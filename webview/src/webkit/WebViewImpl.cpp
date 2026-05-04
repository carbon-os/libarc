#include "WebViewImpl.hpp"

namespace webview {

WebViewImpl::WebViewImpl(NativeHandle handle, WebViewConfig config) {
    devtools_enabled_ = config.devtools;
    setup(handle, config);
}

WebViewImpl::~WebViewImpl() {
    if (webkit_webview_) {
        gtk_widget_destroy(GTK_WIDGET(webkit_webview_));
    }
}

void WebViewImpl::setup(NativeHandle handle, WebViewConfig config) {
    web_context_ = webkit_web_context_get_default();
    
    // 1. Register the custom IPC scheme[cite: 14, 20]
    webkit_web_context_register_uri_scheme(web_context_, "webview",
        (WebKitURISchemeRequestCallback)on_uri_scheme_request, this, nullptr);

    // ── THE CORS FIX ──────────────────────────────────────────────────────────
    // Retrieve the security manager and whitelist our scheme.
    // This stops the "Origin null is not allowed" errors.
    WebKitSecurityManager* security_manager = webkit_web_context_get_security_manager(web_context_);
    webkit_security_manager_register_uri_scheme_as_cors_enabled(security_manager, "webview");
    webkit_security_manager_register_uri_scheme_as_secure(security_manager, "webview");

    ucm_ = webkit_user_content_manager_new();
    webkit_user_content_manager_register_script_message_handler(ucm_, "__wv_console__");
    g_signal_connect(ucm_, "script-message-received::__wv_console__", G_CALLBACK(on_script_message_received), this);

    webkit_webview_ = WEBKIT_WEB_VIEW(g_object_new(WEBKIT_TYPE_WEB_VIEW,
        "user-content-manager", ucm_,
        "web-context", web_context_,
        nullptr));

    find_controller_ = webkit_web_view_get_find_controller(webkit_webview_);

    WebKitSettings* settings = webkit_web_view_get_settings(webkit_webview_);
    webkit_settings_set_enable_developer_extras(settings, devtools_enabled_);

    install_ipc_bridge();

    g_signal_connect(webkit_webview_, "load-changed", G_CALLBACK(on_load_changed), this);
    g_signal_connect(webkit_webview_, "notify::title", G_CALLBACK(on_title_changed), this);
    g_signal_connect(webkit_webview_, "decide-policy", G_CALLBACK(on_decide_policy), this);
    g_signal_connect(webkit_webview_, "permission-request", G_CALLBACK(on_permission_request), this);
    g_signal_connect(webkit_webview_, "authenticate", G_CALLBACK(on_authenticate), this);
    g_signal_connect(webkit_webview_, "close", G_CALLBACK(on_close_requested), this);
    g_signal_connect(web_context_, "download-started", G_CALLBACK(on_download_started), this);

    GtkWidget* parent = static_cast<GtkWidget*>(handle.get());
    if (handle.is_window() || handle.is_view()) {
        gtk_container_add(GTK_CONTAINER(parent), GTK_WIDGET(webkit_webview_));

        // ── THE SIZING FIX ────────────────────────────────────────────────────
        // GtkFixed (from Window.cpp) needs manual sizing for children[cite: 8, 10].
        if (GTK_IS_FIXED(parent)) {
            g_signal_connect(parent, "size-allocate", G_CALLBACK(+[](GtkWidget* widget, GdkRectangle* alloc, gpointer data) {
                GtkWidget* wv_widget = GTK_WIDGET(data);
                gtk_widget_set_size_request(wv_widget, alloc->width, alloc->height);
            }), webkit_webview_);
        }
    }
    
    gtk_widget_show_all(GTK_WIDGET(webkit_webview_));
}

// ── Public WebView constructor / destructor ───────────────────────────────────

WebView::WebView(NativeHandle handle, WebViewConfig config)
    : impl_(std::make_unique<WebViewImpl>(handle, config))
    , ipc(impl_.get())
{}

WebView::~WebView() = default;

} // namespace webview