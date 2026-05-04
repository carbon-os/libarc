#import "WebViewImpl.hpp"
#include <webview/webview.hpp>

namespace webview {

void WebViewImpl::load_url(const std::string& url) {
    is_ipc_content_ = false;
    // ready_ intentionally NOT reset — it means "webview is initialised",
    // not "this navigation finished". Use on_load_finish_cb for that.
    NSURL*        nsUrl   = [NSURL URLWithString:@(url.c_str())];
    NSURLRequest* request = [NSURLRequest requestWithURL:nsUrl];
    [wkwebview_ loadRequest:request];
}

void WebViewImpl::load_html(const std::string& html) {
    is_ipc_content_             = true;
    scheme_handler_.htmlContent = @(html.c_str());
    NSURL* base = [NSURL URLWithString:@"webview://localhost/"];
    [wkwebview_ loadHTMLString:@(html.c_str()) baseURL:base];
}

void WebViewImpl::load_file(const std::string& path) {
    is_ipc_content_ = true;
    NSURL* fileUrl  = [NSURL fileURLWithPath:@(path.c_str())];
    NSURL* dir      = [fileUrl URLByDeletingLastPathComponent];
    [wkwebview_ loadFileURL:fileUrl allowingReadAccessToURL:dir];
}

void WebViewImpl::reload()      { [wkwebview_ reload]; }
void WebViewImpl::go_back()     { [wkwebview_ goBack]; }
void WebViewImpl::go_forward()  { [wkwebview_ goForward]; }

std::string WebViewImpl::get_url() {
    return wkwebview_.URL.absoluteString.UTF8String ?: "";
}

// ── Public WebView forwarders ─────────────────────────────────────────────────

void WebView::load_url(std::string url)   { impl_->load_url(url); }
void WebView::load_html(std::string html) { impl_->load_html(html); }
void WebView::load_file(std::string path) { impl_->load_file(path); }
void WebView::reload()                    { impl_->reload(); }
void WebView::go_back()                   { impl_->go_back(); }
void WebView::go_forward()                { impl_->go_forward(); }
std::string WebView::get_url()            { return impl_->get_url(); }

} // namespace webview