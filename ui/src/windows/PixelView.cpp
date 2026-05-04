#include <windows.h>

#include "ui/PixelView.h"
#include "ui/Window.h"
#include "ui/pixel_channel.h"

#include <algorithm>
#include <mutex>
#include <stdexcept>
#include <vector>

// ── Shared-memory naming ──────────────────────────────────────────────────────
// pixel_channel.h uses POSIX-style names ("/ui_pv_<id>").
// On Windows we use a Local\ named file mapping instead.
static std::wstring win_shm_name(const std::string& channel_id) {
    std::wstring w(channel_id.begin(), channel_id.end());
    return L"Local\\ui_pv_" + w;
}

// ── Timer ID ─────────────────────────────────────────────────────────────────
static constexpr UINT_PTR kPollTimerId = 1;

// ── Impl ──────────────────────────────────────────────────────────────────────
namespace ui {

struct PixelView::Impl {
    HWND hwnd = nullptr;

    // Config
    std::string         channel_id;
    PixelFormat         format        = PixelFormat::BGRA8;
    bool                stretch       = true;
    std::optional<Size> min_size;
    std::optional<Size> max_size;

    // SHM state
    HANDLE   shm_handle   = nullptr;
    void*    shm_ptr      = nullptr;
    size_t   shm_map_size = 0;
    uint64_t last_frame   = UINT64_MAX;
    bool     connected    = false;
    uint64_t frame_count  = 0;

    // Cached frame for WM_PAINT
    std::vector<uint8_t> frame_buf;
    uint32_t             frame_w      = 0;
    uint32_t             frame_h      = 0;
    PixelFormat          frame_fmt    = PixelFormat::BGRA8;

    // Callbacks
    std::function<void(Size)>        cb_resize;
    std::function<void(Point)>       cb_move;
    std::function<void()>            cb_connect;
    std::function<void()>            cb_disconnect;
    std::function<void(FrameEvent&)> cb_frame;

    void try_open();
    void close_channel(bool fire_disconnect);
    void poll();
    void render(const PixelChannelHeader* hdr, const uint8_t* data);
    void paint(HDC hdc) const;

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg,
                                    WPARAM wParam, LPARAM lParam);
};

} // namespace ui

// ── Window-class registration ─────────────────────────────────────────────────
static void register_pixel_view_class() {
    static std::once_flag flag;
    std::call_once(flag, [] {
        WNDCLASSEXW wc   = {};
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = ui::PixelView::Impl::WndProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wc.lpszClassName = L"libui_PixelView";
        RegisterClassExW(&wc);
    });
}

// ── WndProc ───────────────────────────────────────────────────────────────────
namespace ui {

LRESULT CALLBACK PixelView::Impl::WndProc(HWND hwnd, UINT msg,
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

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            if (impl) impl->paint(hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_ERASEBKGND:
            // Suppress — paint() fills the whole rect.
            return 1;

        case WM_TIMER: {
            if (impl && wParam == kPollTimerId) impl->poll();
            return 0;
        }

        case WM_SIZE: {
            if (!impl || !impl->cb_resize) break;
            impl->cb_resize({ (int)LOWORD(lParam), (int)HIWORD(lParam) });
            // Repaint at the new size immediately.
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_MOVE: {
            if (!impl || !impl->cb_move) break;
            impl->cb_move({ (int)(short)LOWORD(lParam),
                            (int)(short)HIWORD(lParam) });
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
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ── Shared-memory helpers ─────────────────────────────────────────────────────

void PixelView::Impl::try_open() {
    HANDLE h = OpenFileMappingW(FILE_MAP_READ, FALSE, win_shm_name(channel_id).c_str());
    if (!h) return;

    // Peek at just the header to learn the total size.
    void* peek = MapViewOfFile(h, FILE_MAP_READ, 0, 0, sizeof(PixelChannelHeader));
    if (!peek) { CloseHandle(h); return; }

    auto* hdr    = static_cast<const PixelChannelHeader*>(peek);
    bool  valid  = (hdr->magic   == kPixelChannelMagic &&
                    hdr->version == kPixelChannelVersion);
    size_t total = sizeof(PixelChannelHeader) + hdr->data_size;
    UnmapViewOfFile(peek);

    if (!valid) { CloseHandle(h); return; }

    void* ptr = MapViewOfFile(h, FILE_MAP_READ, 0, 0, total);
    if (!ptr) { CloseHandle(h); return; }

    shm_handle   = h;
    shm_ptr      = ptr;
    shm_map_size = total;
    last_frame   = UINT64_MAX;
    connected    = true;

    if (cb_connect) cb_connect();
}

void PixelView::Impl::close_channel(bool fire) {
    if (shm_ptr)    { UnmapViewOfFile(shm_ptr);  shm_ptr    = nullptr; }
    if (shm_handle) { CloseHandle(shm_handle);   shm_handle = nullptr; }
    shm_map_size = 0;
    last_frame   = UINT64_MAX;
    if (connected && fire && cb_disconnect) cb_disconnect();
    connected = false;
}

void PixelView::Impl::poll() {
    if (!connected) { try_open(); return; }

    auto* hdr = static_cast<const PixelChannelHeader*>(shm_ptr);

    if (hdr->magic != kPixelChannelMagic) {
        close_channel(/*fire=*/true);
        return;
    }

    uint64_t fc = hdr->frame_count;
    if (fc == last_frame) return;

    // Producer may have grown the buffer — remap silently and retry next tick.
    size_t expected = sizeof(PixelChannelHeader) + hdr->data_size;
    if (expected > shm_map_size) {
        close_channel(/*fire=*/false);
        try_open();
        return;
    }

    const uint8_t* data = static_cast<const uint8_t*>(shm_ptr)
                        + sizeof(PixelChannelHeader);
    render(hdr, data);

    last_frame = fc;
    ++frame_count;

    if (cb_frame) {
        FrameEvent ev { (int)hdr->width, (int)hdr->height,
                        (PixelFormat)hdr->format, frame_count };
        cb_frame(ev);
    }
}

// ── Pixel conversion & paint ──────────────────────────────────────────────────

void PixelView::Impl::render(const PixelChannelHeader* hdr, const uint8_t* data) {
    uint32_t w = hdr->width;
    uint32_t h = hdr->height;
    if (w == 0 || h == 0) return;

    PixelFormat fmt = (PixelFormat)hdr->format;

    // Convert everything to BGRA8 so StretchDIBits always sees the same format.
    std::vector<uint8_t> converted;
    const uint8_t* src = data;

    switch (fmt) {

        case PixelFormat::BGRA8:
            // Native GDI format — copy as-is.
            frame_buf.assign(src, src + (size_t)w * h * 4);
            break;

        case PixelFormat::RGBA8: {
            frame_buf.resize((size_t)w * h * 4);
            const uint8_t* s = src;
            uint8_t*       d = frame_buf.data();
            for (uint32_t i = 0; i < w * h; ++i, s += 4, d += 4) {
                d[0] = s[2]; // B
                d[1] = s[1]; // G
                d[2] = s[0]; // R
                d[3] = s[3]; // A
            }
            break;
        }

        case PixelFormat::RGB8: {
            frame_buf.resize((size_t)w * h * 4);
            const uint8_t* s = src;
            uint8_t*       d = frame_buf.data();
            for (uint32_t i = 0; i < w * h; ++i, s += 3, d += 4) {
                d[0] = s[2]; // B
                d[1] = s[1]; // G
                d[2] = s[0]; // R
                d[3] = 0xFF;
            }
            break;
        }

        case PixelFormat::YUV420: {
            // Planar I420 → BGRA (BT.601 coefficients, matching the macOS path).
            frame_buf.resize((size_t)w * h * 4);
            const uint8_t* Y = src;
            const uint8_t* U = src + (size_t)w * h;
            const uint8_t* V = src + (size_t)w * h + (size_t)(w / 2) * (h / 2);
            uint8_t* d = frame_buf.data();
            for (uint32_t row = 0; row < h; ++row) {
                for (uint32_t col = 0; col < w; ++col) {
                    int y = Y[row * w + col];
                    int u = U[(row / 2) * (w / 2) + (col / 2)] - 128;
                    int v = V[(row / 2) * (w / 2) + (col / 2)] - 128;
                    int r = std::clamp(y + (int)(1.402f * v),                     0, 255);
                    int g = std::clamp(y - (int)(0.344f * u) - (int)(0.714f * v), 0, 255);
                    int b = std::clamp(y + (int)(1.772f * u),                     0, 255);
                    *d++ = (uint8_t)b;
                    *d++ = (uint8_t)g;
                    *d++ = (uint8_t)r;
                    *d++ = 0xFF;
                }
            }
            break;
        }
    }

    frame_w   = w;
    frame_h   = h;
    frame_fmt = fmt;

    InvalidateRect(hwnd, nullptr, FALSE);
}

void PixelView::Impl::paint(HDC hdc) const {
    RECT rc;
    GetClientRect(hwnd, &rc);
    int dw = rc.right;
    int dh = rc.bottom;

    if (frame_buf.empty() || frame_w == 0 || frame_h == 0) {
        FillRect(hdc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
        return;
    }

    // Build a top-down DIB descriptor (negative height = top-down scanlines).
    BITMAPINFO bi   = {};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = (LONG)frame_w;
    bi.bmiHeader.biHeight      = -(LONG)frame_h;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    if (stretch) {
        // Letterbox / pillarbox: preserve aspect ratio, fill remainder in black.
        float src_asp = (float)frame_w / (float)frame_h;
        float dst_asp = (float)dw      / (float)dh;
        int dx = 0, dy = 0, sw = dw, sh = dh;
        if (src_asp > dst_asp) {
            sh = (int)((float)dw / src_asp);
            dy = (dh - sh) / 2;
        } else {
            sw = (int)((float)dh * src_asp);
            dx = (dw - sw) / 2;
        }
        if (dx > 0 || dy > 0)
            FillRect(hdc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
        StretchDIBits(hdc,
                      dx, dy, sw, sh,
                      0, 0, (int)frame_w, (int)frame_h,
                      frame_buf.data(), &bi, DIB_RGB_COLORS, SRCCOPY);
    } else {
        // Center at 1:1.
        int dx = (dw - (int)frame_w) / 2;
        int dy = (dh - (int)frame_h) / 2;
        if (dx != 0 || dy != 0)
            FillRect(hdc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
        SetDIBitsToDevice(hdc,
                          dx, dy,
                          frame_w, frame_h,
                          0, 0, 0, frame_h,
                          frame_buf.data(), &bi, DIB_RGB_COLORS);
    }
}

// ── PixelView public API ──────────────────────────────────────────────────────

PixelView::PixelView(Window& parent, PixelViewConfig config)
    : impl_(std::make_unique<Impl>())
{
    register_pixel_view_class();

    impl_->channel_id = config.channel_id;
    impl_->format     = config.format;
    impl_->stretch    = config.stretch;
    impl_->min_size   = config.min_size;
    impl_->max_size   = config.max_size;

    HWND parent_hwnd = reinterpret_cast<HWND>(parent.native_handle().get());

    HWND hwnd = CreateWindowExW(
        0,
        L"libui_PixelView",
        L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        config.position.x, config.position.y,
        config.size.width,  config.size.height,
        parent_hwnd,
        nullptr,
        GetModuleHandleW(nullptr),
        impl_.get()
    );

    if (!hwnd)
        throw std::runtime_error("libui: failed to create PixelView");

    impl_->hwnd = hwnd;

    SetTimer(hwnd, kPollTimerId, (UINT)config.poll_interval_ms, nullptr);
}

PixelView::~PixelView() {
    if (impl_->hwnd && IsWindow(impl_->hwnd)) {
        KillTimer(impl_->hwnd, kPollTimerId);
        impl_->close_channel(/*fire=*/false);
        SetWindowLongPtrW(impl_->hwnd, GWLP_USERDATA, 0);
        DestroyWindow(impl_->hwnd);
        impl_->hwnd = nullptr;
    }
}

// ── Geometry ──────────────────────────────────────────────────────────────────

void PixelView::set_size(Size size) {
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

Size PixelView::get_size() const {
    RECT rc;
    GetClientRect(impl_->hwnd, &rc);
    return { rc.right, rc.bottom };
}

void PixelView::set_position(Point point) {
    SetWindowPos(impl_->hwnd, nullptr, point.x, point.y, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

Point PixelView::get_position() const {
    RECT rc;
    GetWindowRect(impl_->hwnd, &rc);
    HWND parent = GetParent(impl_->hwnd);
    POINT pt = { rc.left, rc.top };
    if (parent) ScreenToClient(parent, &pt);
    return { pt.x, pt.y };
}

void PixelView::set_min_size(Size size) { impl_->min_size = size; }
void PixelView::set_max_size(Size size) { impl_->max_size = size; }

// ── State ─────────────────────────────────────────────────────────────────────

void     PixelView::show()            { ShowWindow(impl_->hwnd, SW_SHOW); }
void     PixelView::hide()            { ShowWindow(impl_->hwnd, SW_HIDE); }
bool     PixelView::is_visible()  const { return IsWindowVisible(impl_->hwnd) != FALSE; }
bool     PixelView::is_connected() const { return impl_->connected; }
uint64_t PixelView::get_frame_count() const { return impl_->frame_count; }

// ── Events ────────────────────────────────────────────────────────────────────

void PixelView::on_resize    (std::function<void(Size)>        fn) { impl_->cb_resize     = std::move(fn); }
void PixelView::on_move      (std::function<void(Point)>       fn) { impl_->cb_move       = std::move(fn); }
void PixelView::on_connect   (std::function<void()>            fn) { impl_->cb_connect    = std::move(fn); }
void PixelView::on_disconnect(std::function<void()>            fn) { impl_->cb_disconnect = std::move(fn); }
void PixelView::on_frame     (std::function<void(FrameEvent&)> fn) { impl_->cb_frame      = std::move(fn); }

// ── NativeHandle ──────────────────────────────────────────────────────────────

NativeHandle PixelView::native_handle() const {
    return { impl_->hwnd, NativeHandleType::HWND };
}

} // namespace ui