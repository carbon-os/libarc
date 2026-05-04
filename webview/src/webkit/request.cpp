#include "WebViewImpl.hpp"

namespace webview {

// Note: WebKitGTK 4.1 / 6.0 does not inherently support synchronous request interception 
// blocking for standard web traffic without writing a custom WebProcess extension.
// The custom "webview://" scheme is already intercepted via on_uri_scheme_request in ipc.cpp.
// This setter is provided for API parity.

void WebView::on_request(std::function<void(ResourceRequest&)> fn) {
    impl_->on_request_cb = std::move(fn);
}

} // namespace webview