#pragma once

#include <ui/ui.h>
#include <webview/webview.hpp>

#if defined(__APPLE__)
#  include <objc/message.h>
#  include <objc/runtime.h>
#endif

inline webview::NativeHandle to_webview_handle(ui::NativeHandle h) {
    webview::NativeHandleType wt;
    switch (h.type()) {
        case ui::NativeHandleType::NSView:
            wt = webview::NativeHandleType::NSView;
            break;
        case ui::NativeHandleType::HWND:
            wt = webview::NativeHandleType::HWND;
            break;
        case ui::NativeHandleType::GtkWidget:
            wt = webview::NativeHandleType::GtkWidget;
            break;

        case ui::NativeHandleType::NSWindow: {
            // The WebView needs an NSView, not an NSWindow.
            // Ask the window for its contentView and use that pointer instead.
#if defined(__APPLE__)
            void* content_view =
                reinterpret_cast<void* (*)(void*, SEL)>(objc_msgSend)(
                    h.get(), sel_registerName("contentView"));
            return webview::NativeHandle(content_view,
                                         webview::NativeHandleType::NSView);
#else
            wt = webview::NativeHandleType::NSView;
            break;
#endif
        }

        case ui::NativeHandleType::GtkWindow:
            wt = webview::NativeHandleType::GtkWidget;
            break;
    }
    return webview::NativeHandle(h.get(), wt);
}