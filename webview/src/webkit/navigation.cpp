#include "WebViewImpl.hpp"

namespace webview {

void WebViewImpl::load_url(const std::string& url) {
    // IPC is not available for external URLs — clear any pending content
    // so the scheme handler never accidentally serves stale HTML/files
    // against a load_url-initiated origin.
    pending_html_.clear();
    pending_file_path_.clear();
    ready_ = false;
    webkit_web_view_load_uri(webkit_webview_, url.c_str());
}

void WebViewImpl::load_html(const std::string& html) {
    ready_ = false;
    pending_file_path_.clear();
    pending_html_ = html;
    webkit_web_view_load_uri(webkit_webview_, "webview://app/");
}

void WebViewImpl::load_file(const std::string& path) {
    ready_ = false;
    pending_html_.clear();
    pending_file_path_ = path;
    webkit_web_view_load_uri(webkit_webview_, "webview://app/");
}

void WebViewImpl::reload() { webkit_web_view_reload(webkit_webview_); }
void WebViewImpl::go_back() { webkit_web_view_go_back(webkit_webview_); }
void WebViewImpl::go_forward() { webkit_web_view_go_forward(webkit_webview_); }

std::string WebViewImpl::get_url() {
    const gchar* uri = webkit_web_view_get_uri(webkit_webview_);
    return uri ? uri : "";
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