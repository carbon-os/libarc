#include "WebViewImpl.hpp"
#include <webview/webview.hpp>

namespace webview {

// on_request registers a callback for all non-IPC, non-content resource requests.
// The filter for webview://* is already registered during setup; for general
// http/https interception an additional broad filter is added here.

void WebView::on_request(std::function<void(ResourceRequest&)> fn) {
    impl_->on_request_cb = std::move(fn);

    if (impl_->on_request_cb && impl_->webview_) {
        // Intercept all resource requests so on_request_cb can inspect/modify them.
        impl_->webview_->AddWebResourceRequestedFilter(
            L"*",
            COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
    }
}

} // namespace webview