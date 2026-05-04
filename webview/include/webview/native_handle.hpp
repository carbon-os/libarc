#pragma once
#include <cstddef>

namespace webview {

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

    void*            get()       const { return handle_; }
    NativeHandleType type()      const { return type_; }

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

} // namespace webview