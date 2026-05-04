#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>

#include "ui/Window.h"

#include <algorithm>
#include <mutex>
#include <stdexcept>
#include <string>

// ── Windows 11 system backdrop constants ──────────────────────────────────────
// These ship in newer SDK headers; guard so we compile against older SDKs too.
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#  define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
enum {
    DWMSBT_NONE_PRIV            = 1,
    DWMSBT_MAINWINDOW_PRIV      = 2,   // Mica
    DWMSBT_TRANSIENTWINDOW_PRIV = 3,   // Acrylic
    DWMSBT_TABBEDWINDOW_PRIV    = 4,   // MicaAlt
};

// ── UTF-8 ↔ wide helpers ──────────────────────────────────────────────────────
namespace {

std::wstring to_wstring(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

std::string from_wstring(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(),
                                nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(),
                        s.data(), n, nullptr, nullptr);
    return s;
}

} // namespace

// ── Impl ──────────────────────────────────────────────────────────────────────
namespace ui {

struct Window::Impl {
    HWND hwnd           = nullptr;
    bool transparent    = false;
    bool always_on_top  = false;
    bool fullscreen     = false;
    RECT pre_fs_rect    = {};
    LONG pre_fs_style   = 0;
    LONG pre_fs_exstyle = 0;

    std::optional<Size> min_size;
    std::optional<Size> max_size;

    std::function<void(Size)>        cb_resize;
    std::function<void(Point)>       cb_move;
    std::function<void()>            cb_focus;
    std::function<void()>            cb_blur;
    std::function<void(WindowState)> cb_state_change;
    std::function<bool()>            cb_close;
    std::function<void(DropEvent&)>  cb_drop;

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg,
                                    WPARAM wParam, LPARAM lParam);
};

} // namespace ui

// ── Window-class registration (once per process) ──────────────────────────────
static void register_window_class() {
    static std::once_flag flag;
    std::call_once(flag, [] {
        WNDCLASSEXW wc   = {};
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = ui::Window::Impl::WndProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"libui_Window";
        RegisterClassExW(&wc);
    });
}

// ── Style builders ────────────────────────────────────────────────────────────
static DWORD build_style(ui::WindowStyle style, bool resizable) {
    switch (style) {
        case ui::WindowStyle::Default: {
            DWORD s = WS_OVERLAPPEDWINDOW;
            if (!resizable) s &= ~(DWORD)(WS_THICKFRAME | WS_MAXIMIZEBOX);
            return s;
        }
        case ui::WindowStyle::BorderOnly:
            return WS_POPUP | WS_THICKFRAME;
        case ui::WindowStyle::Borderless:
            return WS_POPUP;
    }
    return WS_OVERLAPPEDWINDOW;
}

static DWORD build_exstyle(bool transparent, bool always_on_top) {
    DWORD ex = WS_EX_ACCEPTFILES;
    if (transparent)   ex |= WS_EX_LAYERED;
    if (always_on_top) ex |= WS_EX_TOPMOST;
    return ex;
}

// ── Backdrop helpers ──────────────────────────────────────────────────────────
static void apply_window_effect(HWND hwnd, ui::BackdropEffect effect) {
    // Extend the DWM frame to cover the whole client area so the system
    // backdrop is visible through it.
    MARGINS m = {-1, -1, -1, -1};
    DwmExtendFrameIntoClientArea(hwnd, &m);

    INT type = DWMSBT_NONE_PRIV;
    switch (effect) {
        case ui::BackdropEffect::Acrylic:  type = DWMSBT_TRANSIENTWINDOW_PRIV; break;
        case ui::BackdropEffect::Mica:     type = DWMSBT_MAINWINDOW_PRIV;      break;
        case ui::BackdropEffect::MicaAlt:  type = DWMSBT_TABBEDWINDOW_PRIV;   break;
        case ui::BackdropEffect::Vibrancy: type = DWMSBT_MAINWINDOW_PRIV;      break; // macOS-only; Mica is the closest
    }
    DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &type, sizeof(type));
}

static void clear_window_effect(HWND hwnd) {
    INT type = DWMSBT_NONE_PRIV;
    DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &type, sizeof(type));
    MARGINS m = {0, 0, 0, 0};
    DwmExtendFrameIntoClientArea(hwnd, &m);
}

// ── WndProc ───────────────────────────────────────────────────────────────────
namespace ui {

LRESULT CALLBACK Window::Impl::WndProc(HWND hwnd, UINT msg,
                                        WPARAM wParam, LPARAM lParam) {
    Impl* impl = reinterpret_cast<Impl*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {

        case WM_NCCREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        case WM_SIZE: {
            if (!impl) break;
            int w = (int)LOWORD(lParam);
            int h = (int)HIWORD(lParam);
            if (impl->cb_resize && wParam != SIZE_MINIMIZED)
                impl->cb_resize({ w, h });
            if (impl->cb_state_change && !impl->fullscreen) {
                WindowState st = WindowState::Normal;
                if      (wParam == SIZE_MINIMIZED) st = WindowState::Minimized;
                else if (wParam == SIZE_MAXIMIZED) st = WindowState::Maximized;
                impl->cb_state_change(st);
            }
            return 0;
        }

        case WM_MOVE: {
            if (!impl || !impl->cb_move) break;
            // Use GetWindowRect so the reported coordinates match get_position().
            RECT rc;
            GetWindowRect(hwnd, &rc);
            impl->cb_move({ rc.left, rc.top });
            return 0;
        }

        case WM_SETFOCUS: {
            if (impl && impl->cb_focus) impl->cb_focus();
            return 0;
        }

        case WM_KILLFOCUS: {
            if (impl && impl->cb_blur) impl->cb_blur();
            return 0;
        }

        case WM_GETMINMAXINFO: {
            if (!impl) break;
            auto* mmi  = reinterpret_cast<MINMAXINFO*>(lParam);
            DWORD sty  = (DWORD)GetWindowLongW(hwnd, GWL_STYLE);
            DWORD exsty = (DWORD)GetWindowLongW(hwnd, GWL_EXSTYLE);

            // Convert a client-area size to a window (frame) size.
            auto client_to_window = [&](Size s) -> POINT {
                RECT rc = { 0, 0, s.width, s.height };
                AdjustWindowRectEx(&rc, sty, FALSE, exsty);
                return { rc.right - rc.left, rc.bottom - rc.top };
            };

            if (impl->min_size) {
                auto p = client_to_window(*impl->min_size);
                mmi->ptMinTrackSize = p;
            }
            if (impl->max_size) {
                auto p = client_to_window(*impl->max_size);
                mmi->ptMaxTrackSize = p;
            }
            return 0;
        }

        case WM_CLOSE: {
            if (impl && impl->cb_close && !impl->cb_close()) return 0;
            // Null out the user-data pointer before destroying so no callbacks
            // fire against the Impl after we start tearing down.
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            DestroyWindow(hwnd);
            return 0;
        }

        case WM_DESTROY: {
            PostQuitMessage(0);
            return 0;
        }

        case WM_DROPFILES: {
            if (!impl || !impl->cb_drop) break;
            HDROP drop = reinterpret_cast<HDROP>(wParam);
            UINT  n    = DragQueryFileW(drop, 0xFFFFFFFFu, nullptr, 0);
            DropEvent ev;
            for (UINT i = 0; i < n; ++i) {
                UINT len = DragQueryFileW(drop, i, nullptr, 0);
                std::wstring buf(len, L'\0');
                DragQueryFileW(drop, i, buf.data(), len + 1);
                ev.paths.push_back(from_wstring(buf));
            }
            POINT pt;
            DragQueryPoint(drop, &pt);
            ev.position = { pt.x, pt.y };
            DragFinish(drop);
            impl->cb_drop(ev);
            return 0;
        }

        case WM_ERASEBKGND: {
            // Suppress background erase for transparent windows to avoid flicker.
            if (impl && impl->transparent) return 1;
            break;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ── Constructor / destructor ──────────────────────────────────────────────────

Window::Window(WindowConfig config)
    : impl_(std::make_unique<Impl>())
{
    register_window_class();

    impl_->transparent   = config.transparent;
    impl_->always_on_top = config.always_on_top;
    impl_->min_size      = config.min_size;
    impl_->max_size      = config.max_size;

    DWORD style   = build_style(config.style, config.resizable);
    DWORD exstyle = build_exstyle(config.transparent, config.always_on_top);

    // Expand the requested client size to a window frame size.
    RECT rc = { 0, 0, config.size.width, config.size.height };
    AdjustWindowRectEx(&rc, style, FALSE, exstyle);
    int win_w = rc.right  - rc.left;
    int win_h = rc.bottom - rc.top;

    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    if (config.position) { x = config.position->x; y = config.position->y; }

    HWND hwnd = CreateWindowExW(
        exstyle,
        L"libui_Window",
        to_wstring(config.title).c_str(),
        style | WS_VISIBLE,
        x, y, win_w, win_h,
        nullptr, nullptr,
        GetModuleHandleW(nullptr),
        impl_.get()   // forwarded to WM_NCCREATE → GWLP_USERDATA
    );

    if (!hwnd)
        throw std::runtime_error("libui: CreateWindowExW failed");

    impl_->hwnd = hwnd;

    if (config.transparent) {
        // Enable DWM transparency; opaque layered alpha keeps child controls usable.
        SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
        DWM_BLURBEHIND bb = {};
        bb.dwFlags  = DWM_BB_ENABLE;
        bb.fEnable  = TRUE;
        DwmEnableBlurBehindWindow(hwnd, &bb);
    }

    if (!config.position)
        center();  // let the OS pick a screen, then center on it

    if (config.effect)
        apply_window_effect(hwnd, *config.effect);
}

Window::~Window() {
    if (impl_->hwnd && IsWindow(impl_->hwnd)) {
        // Prevent WndProc callbacks from firing against a dead Impl.
        SetWindowLongPtrW(impl_->hwnd, GWLP_USERDATA, 0);
        DestroyWindow(impl_->hwnd);
        impl_->hwnd = nullptr;
    }
}

// ── Geometry ──────────────────────────────────────────────────────────────────

void Window::set_size(Size size) {
    DWORD sty  = (DWORD)GetWindowLongW(impl_->hwnd, GWL_STYLE);
    DWORD exsty = (DWORD)GetWindowLongW(impl_->hwnd, GWL_EXSTYLE);
    RECT rc = { 0, 0, size.width, size.height };
    AdjustWindowRectEx(&rc, sty, FALSE, exsty);
    SetWindowPos(impl_->hwnd, nullptr, 0, 0,
                 rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

Size Window::get_size() const {
    RECT rc;
    GetClientRect(impl_->hwnd, &rc);
    return { rc.right, rc.bottom };
}

void Window::set_position(Point point) {
    SetWindowPos(impl_->hwnd, nullptr, point.x, point.y, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

Point Window::get_position() const {
    RECT rc;
    GetWindowRect(impl_->hwnd, &rc);
    return { rc.left, rc.top };
}

void Window::center() {
    HMONITOR mon = MonitorFromWindow(impl_->hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(mon, &mi);
    RECT& wa = mi.rcWork;
    RECT  wr;
    GetWindowRect(impl_->hwnd, &wr);
    int w = wr.right  - wr.left;
    int h = wr.bottom - wr.top;
    int x = wa.left + (wa.right  - wa.left - w) / 2;
    int y = wa.top  + (wa.bottom - wa.top  - h) / 2;
    SetWindowPos(impl_->hwnd, nullptr, x, y, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void Window::set_min_size(Size size) { impl_->min_size = size; }
void Window::set_max_size(Size size) { impl_->max_size = size; }

// ── State ─────────────────────────────────────────────────────────────────────

void Window::show()     { ShowWindow(impl_->hwnd, SW_SHOW);     }
void Window::hide()     { ShowWindow(impl_->hwnd, SW_HIDE);     }
void Window::focus()    { SetForegroundWindow(impl_->hwnd);     }
void Window::minimize() { ShowWindow(impl_->hwnd, SW_MINIMIZE); }
void Window::maximize() { ShowWindow(impl_->hwnd, SW_MAXIMIZE); }
void Window::restore()  { ShowWindow(impl_->hwnd, SW_RESTORE);  }

void Window::run() {
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void Window::set_fullscreen(bool fullscreen) {
    if (fullscreen == impl_->fullscreen) return;

    if (fullscreen) {
        impl_->pre_fs_style  = GetWindowLongW(impl_->hwnd, GWL_STYLE);
        impl_->pre_fs_exstyle = GetWindowLongW(impl_->hwnd, GWL_EXSTYLE);
        GetWindowRect(impl_->hwnd, &impl_->pre_fs_rect);

        HMONITOR mon = MonitorFromWindow(impl_->hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfoW(mon, &mi);
        RECT& sr = mi.rcMonitor;

        SetWindowLongW(impl_->hwnd, GWL_STYLE,   WS_POPUP | WS_VISIBLE);
        SetWindowLongW(impl_->hwnd, GWL_EXSTYLE, 0);
        SetWindowPos(impl_->hwnd, HWND_TOP,
                     sr.left, sr.top,
                     sr.right  - sr.left,
                     sr.bottom - sr.top,
                     SWP_FRAMECHANGED | SWP_NOACTIVATE);
        impl_->fullscreen = true;
        if (impl_->cb_state_change)
            impl_->cb_state_change(WindowState::Fullscreen);
    } else {
        SetWindowLongW(impl_->hwnd, GWL_STYLE,   impl_->pre_fs_style);
        SetWindowLongW(impl_->hwnd, GWL_EXSTYLE, impl_->pre_fs_exstyle);
        RECT& r = impl_->pre_fs_rect;
        SetWindowPos(impl_->hwnd, nullptr,
                     r.left, r.top,
                     r.right - r.left, r.bottom - r.top,
                     SWP_FRAMECHANGED | SWP_NOACTIVATE);
        impl_->fullscreen = false;
        if (impl_->cb_state_change)
            impl_->cb_state_change(WindowState::Normal);
    }
}

bool Window::is_fullscreen() const { return impl_->fullscreen; }

bool Window::is_visible() const {
    return IsWindowVisible(impl_->hwnd) != FALSE;
}

bool Window::is_focused() const {
    return GetForegroundWindow() == impl_->hwnd;
}

WindowState Window::get_state() const {
    if (impl_->fullscreen) return WindowState::Fullscreen;
    WINDOWPLACEMENT wp = { sizeof(wp) };
    GetWindowPlacement(impl_->hwnd, &wp);
    if (wp.showCmd == SW_SHOWMINIMIZED) return WindowState::Minimized;
    if (wp.showCmd == SW_SHOWMAXIMIZED) return WindowState::Maximized;
    return WindowState::Normal;
}

// ── Appearance ────────────────────────────────────────────────────────────────

void Window::set_title(std::string title) {
    SetWindowTextW(impl_->hwnd, to_wstring(title).c_str());
}

std::string Window::get_title() const {
    int len = GetWindowTextLengthW(impl_->hwnd);
    if (len <= 0) return {};
    std::wstring w(len, L'\0');
    GetWindowTextW(impl_->hwnd, w.data(), len + 1);
    return from_wstring(w);
}

void Window::set_always_on_top(bool on_top) {
    impl_->always_on_top = on_top;
    SetWindowPos(impl_->hwnd,
                 on_top ? HWND_TOPMOST : HWND_NOTOPMOST,
                 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void Window::set_effect(BackdropEffect effect) {
    apply_window_effect(impl_->hwnd, effect);
}

void Window::clear_effect() {
    clear_window_effect(impl_->hwnd);
}

// ── Events ────────────────────────────────────────────────────────────────────

void Window::on_resize      (std::function<void(Size)>        fn) { impl_->cb_resize       = std::move(fn); }
void Window::on_move        (std::function<void(Point)>       fn) { impl_->cb_move         = std::move(fn); }
void Window::on_focus       (std::function<void()>            fn) { impl_->cb_focus        = std::move(fn); }
void Window::on_blur        (std::function<void()>            fn) { impl_->cb_blur         = std::move(fn); }
void Window::on_state_change(std::function<void(WindowState)> fn) { impl_->cb_state_change = std::move(fn); }
void Window::on_close       (std::function<bool()>            fn) { impl_->cb_close        = std::move(fn); }
void Window::on_drop        (std::function<void(DropEvent&)>  fn) { impl_->cb_drop         = std::move(fn); }

// ── NativeHandle ──────────────────────────────────────────────────────────────

NativeHandle Window::native_handle() const {
    return { impl_->hwnd, NativeHandleType::HWND };
}

} // namespace ui