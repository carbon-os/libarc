#import "WebViewImpl.hpp"
#include <webview/webview.hpp>

namespace webview {

void WebViewImpl::load_url(const std::string& url) {
    is_ipc_content_ = false;
    NSURL*        nsUrl   = [NSURL URLWithString:@(url.c_str())];
    NSURLRequest* request = [NSURLRequest requestWithURL:nsUrl];
    [wkwebview_ loadRequest:request];
}

void WebViewImpl::load_html(const std::string& html) {
    is_ipc_content_             = true;
    scheme_handler_.htmlContent = @(html.c_str());
    // Route through our scheme handler so on_request_cb and resource_root
    // are both active. Absolute asset paths (/assets/…) resolve correctly
    // against the webview://app/ origin.
    [wkwebview_ loadRequest:[NSURLRequest requestWithURL:
        [NSURL URLWithString:@"webview://app/"]]];
}

void WebViewImpl::load_file(const std::string& path) {
    is_ipc_content_ = true;
    NSURL* fileUrl  = [NSURL fileURLWithPath:@(path.c_str())];

    // Derive resource root from the file's containing directory so that
    // sibling assets (/assets/…) are resolved and served correctly.
    resource_root_ = fileUrl.URLByDeletingLastPathComponent
                              .path.UTF8String ?: "";

    NSString* html = [NSString stringWithContentsOfURL:fileUrl
                                              encoding:NSUTF8StringEncoding
                                                 error:nil] ?: @"";
    scheme_handler_.htmlContent = html;
    [wkwebview_ loadRequest:[NSURLRequest requestWithURL:
        [NSURL URLWithString:@"webview://app/"]]];
}

void WebViewImpl::reload()     { [wkwebview_ reload]; }
void WebViewImpl::go_back()    { [wkwebview_ goBack]; }
void WebViewImpl::go_forward() { [wkwebview_ goForward]; }

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