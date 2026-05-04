#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ui {

// ── Geometry ──────────────────────────────────────────────────────────────────

struct Size  { int width;  int height; };
struct Point { int x;     int y;      };

// ── Window enums ─────────────────────────────────────────────────────────────

enum class WindowState {
    Normal,
    Minimized,
    Maximized,
    Fullscreen,
};

enum class WindowStyle {
    Default,     // full native decoration
    BorderOnly,  // border + resize handle, no title bar
    Borderless,  // completely undecorated
};

enum class BackdropEffect {
    Vibrancy,  // macOS — NSVisualEffectView
    Acrylic,   // Windows — Acrylic blur-behind
    Mica,      // Windows — Mica
    MicaAlt,   // Windows — high-contrast Mica
};

// ── PixelView ─────────────────────────────────────────────────────────────────

enum class PixelFormat {
    BGRA8,   // 8-bit per channel, BGRA
    RGBA8,   // 8-bit per channel, RGBA
    RGB8,    // 8-bit per channel, no alpha
    YUV420,  // planar YUV
};

struct FrameEvent {
    int         width;
    int         height;
    PixelFormat format;
    uint64_t    frame_count;
};

// ── Drop ─────────────────────────────────────────────────────────────────────

struct DropEvent {
    std::vector<std::string> paths;
    Point                    position;
};

// ── NativeHandle ─────────────────────────────────────────────────────────────

enum class NativeHandleType {
    NSWindow,
    NSView,
    HWND,
    GtkWindow,
    GtkWidget,
};

class NativeHandle {
public:
    NativeHandle(void* handle, NativeHandleType type)
        : handle_(handle), type_(type) {}

    void*            get()  const { return handle_; }
    void*            ptr()  const { return handle_; } // alias for ergonomics
    NativeHandleType type() const { return type_; }

    bool is_window() const {
        return type_ == NativeHandleType::NSWindow
            || type_ == NativeHandleType::HWND
            || type_ == NativeHandleType::GtkWindow;
    }
    bool is_view() const {
        return type_ == NativeHandleType::NSView
            || type_ == NativeHandleType::GtkWidget;
    }

private:
    void*            handle_;
    NativeHandleType type_;
};

} // namespace ui