#include "WebViewImpl.hpp"

namespace webview {

// Note: Media capture permissions are handled via the "permission-request" WebKit signal 
// inside events.cpp alongside geolocation. 
// This file exists to map the on_permission_request forwarder and maintain folder structure parity.

gboolean WebViewImpl::on_permission_request(WebKitWebView*, WebKitPermissionRequest* request, gpointer user_data) {
    auto* self = static_cast<WebViewImpl*>(user_data);
    if (!self->on_permission_cb) {
        webkit_permission_request_deny(request);
        return TRUE;
    }

    PermissionRequest req;
    req.origin = ""; 

    if (WEBKIT_IS_GEOLOCATION_PERMISSION_REQUEST(request)) {
        req.permission = PermissionType::Geolocation;
    } else if (WEBKIT_IS_USER_MEDIA_PERMISSION_REQUEST(request)) {
        gboolean audio = webkit_user_media_permission_is_for_audio_device(WEBKIT_USER_MEDIA_PERMISSION_REQUEST(request));
        req.permission = audio ? PermissionType::Microphone : PermissionType::Camera;
    } else {
        webkit_permission_request_deny(request);
        return TRUE;
    }

    self->on_permission_cb(req);

    if (req.is_granted()) {
        webkit_permission_request_allow(request);
    } else {
        webkit_permission_request_deny(request);
    }
    return TRUE;
}

void WebView::on_permission_request(std::function<void(PermissionRequest&)> fn) {
    impl_->on_permission_cb = std::move(fn);
}

} // namespace webview