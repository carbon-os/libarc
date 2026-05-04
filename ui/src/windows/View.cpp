#include <windows.h>
#include <dwmapi.h>

#include "ui/View.h"
#include "ui/Window.h"

#include <algorithm>
#include <mutex>
#include <stdexcept>

// ── Impl ──────────────────────────────────────────────────────────────────────
namespace ui {

struct View::Impl {
    HWND hwnd = nullptr;

    std::optional<Size> min_size;
    std::optional<Size> max_size;

    std::function<void(Size)>  cb_resize;
    std::function<void(Point)> cb_move;
    std::function<void()>      cb_focus;
    std::function<void()>      cb_blur;

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg,
                                    WPARAM wParam, LPARAM lParam);
};

} // namespace ui

// ── Window-class registration ─────────────────────────────────────────────────
static void register_view_class() {
    static std::once_flag flag;
    std::call_once(flag, [] {
        WNDCLASSEXW wc   = {};
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = ui::View::Impl::WndProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = L"libui_View";
        RegisterClassExW(&wc);
    });
}

// ── Backdrop helper ───────────────────────────────────────────────────────────
static void apply_view_effect(HWND hwnd, ui::BackdropEffect /*effect*/) {
    DWM_BLURBEHIND bb = {};
    bb.dwFlags  = DWM_BB_ENABLE;
    bb.fEnable  = TRUE;
    DwmEnableBlurBehindWindow(hwnd, &bb);
}

static void clear_view_effect(HWND hwnd) {
    DWM_BLURBEHIND bb = {};
    bb.dwFlags  = DWM_BB_ENABLE;
    bb.fEnable  = FALSE;
    DwmEnableBlurBehindWindow(hwnd, &bb);
}

// ── WndProc ───────────────────────────────────────────────────────────────────
namespace ui {

LRESULT CALLBACK View::Impl::WndProc(HWND hwnd, UINT msg,
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
            if (!impl || !impl->cb_resize) break;
            impl->cb_resize({ (int)LOWORD(lParam), (int)HIWORD(lParam) });
            return 0;
        }

        case WM_MOVE: {
            if (!impl || !impl->cb_move) break;
            impl->cb_move({ (int)(short)LOWORD(lParam),
                            (int)(short)HIWORD(lParam) });
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
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            if (impl->min_size) {
                mmi->ptMinTrackSize.x = impl->min_size->width;
                mmi->ptMinTrackSize.y = impl->min_size->height;
            }
            if (impl->max_size) {
                mmi->ptMaxTrackSize.x = impl->max_size->width;
                mmi->ptMaxTrackSize.y = impl->max_size->height;
            }
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ── Constructor / destructor ──────────────────────────────────────────────────

View::View(Window& parent, ViewConfig config)
    : impl_(std::make_unique<Impl>())
{
    register_view_class();

    impl_->min_size = config.min_size;
    impl_->max_size = config.max_size;

    HWND parent_hwnd = reinterpret_cast<HWND>(parent.native_handle().get());

    DWORD style   = WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
    DWORD exstyle = 0;
    if (config.transparent) exstyle |= WS_EX_TRANSPARENT;

    HWND hwnd = CreateWindowExW(
        exstyle,
        L"libui_View",
        L"",
        style,
        config.position.x, config.position.y,
        config.size.width,  config.size.height,
        parent_hwnd,
        nullptr,
        GetModuleHandleW(nullptr),
        impl_.get()
    );

    if (!hwnd)
        throw std::runtime_error("libui: failed to create View");

    impl_->hwnd = hwnd;

    if (config.effect)
        apply_view_effect(hwnd, *config.effect);
}

View::~View() {
    if (impl_->hwnd && IsWindow(impl_->hwnd)) {
        SetWindowLongPtrW(impl_->hwnd, GWLP_USERDATA, 0);
        DestroyWindow(impl_->hwnd);
        impl_->hwnd = nullptr;
    }
}

// ── Geometry ──────────────────────────────────────────────────────────────────

void View::set_size(Size size) {
    if (impl_->min_size) {
        size.width  = std::max(size.width,  impl_->min_size->width);
        size.height = std::max(size.height, impl_->min_size->height);
    }
    if (impl_->max_size) {
        size.width  = std::min(size.width,  impl_->max_size->width);
        size.height = std::min(size.height, impl_->max_size->height);
    }
    SetWindowPos(impl_->hwnd, nullptr, 0, 0, size.width, size.height,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

Size View::get_size() const {
    RECT rc;
    GetClientRect(impl_->hwnd, &rc);
    return { rc.right, rc.bottom };
}

void View::set_position(Point point) {
    SetWindowPos(impl_->hwnd, nullptr, point.x, point.y, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

Point View::get_position() const {
    RECT rc;
    GetWindowRect(impl_->hwnd, &rc);
    HWND parent = GetParent(impl_->hwnd);
    POINT pt = { rc.left, rc.top };
    if (parent) ScreenToClient(parent, &pt);
    return { pt.x, pt.y };
}

void View::set_min_size(Size size) { impl_->min_size = size; }
void View::set_max_size(Size size) { impl_->max_size = size; }

// ── State ─────────────────────────────────────────────────────────────────────

void View::show()  { ShowWindow(impl_->hwnd, SW_SHOW); }
void View::hide()  { ShowWindow(impl_->hwnd, SW_HIDE); }
void View::focus() { SetFocus(impl_->hwnd); }

bool View::is_visible() const { return IsWindowVisible(impl_->hwnd) != FALSE; }
bool View::is_focused()  const { return GetFocus() == impl_->hwnd; }

// ── Stacking ──────────────────────────────────────────────────────────────────

void View::bring_to_front() {
    SetWindowPos(impl_->hwnd, HWND_TOP,
                 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void View::send_to_back() {
    SetWindowPos(impl_->hwnd, HWND_BOTTOM,
                 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

// ── Appearance ────────────────────────────────────────────────────────────────

void View::set_effect(BackdropEffect effect) {
    apply_view_effect(impl_->hwnd, effect);
}

void View::clear_effect() {
    clear_view_effect(impl_->hwnd);
}

// ── Events ────────────────────────────────────────────────────────────────────

void View::on_resize(std::function<void(Size)>  fn) { impl_->cb_resize = std::move(fn); }
void View::on_move  (std::function<void(Point)> fn) { impl_->cb_move   = std::move(fn); }
void View::on_focus (std::function<void()>      fn) { impl_->cb_focus  = std::move(fn); }
void View::on_blur  (std::function<void()>      fn) { impl_->cb_blur   = std::move(fn); }

// ── NativeHandle ──────────────────────────────────────────────────────────────

NativeHandle View::native_handle() const {
    return { impl_->hwnd, NativeHandleType::HWND };
}

} // namespace ui