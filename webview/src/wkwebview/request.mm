#import "WebViewImpl.hpp"
#include <webview/webview.hpp>

// WebViewInterceptSchemeHandler has been removed. Request interception for
// webview://app/* is now handled directly inside WebViewSchemeHandler
// (WebViewImpl.mm), which calls on_request_cb before file serving.

namespace webview {

void WebView::on_request(std::function<void(ResourceRequest&)> fn) {
    impl_->on_request_cb = std::move(fn);
}

} // namespace webview