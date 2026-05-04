#include "WebViewImpl.hpp"
#include "string_util.hpp"
#include <webview/webview.hpp>

namespace webview {

using namespace detail;

void WebViewImpl::load_url(const std::string& url) {
    is_ipc_content_ = false;
    ready_          = false;
    pending_html_.clear();
    webview_->Navigate(to_wide(url).c_str());
}

// load_html: store the HTML, then navigate to webview://localhost/ so the page
// gets the webview:// origin needed for same-origin IPC fetch() calls.
void WebViewImpl::load_html(const std::string& html) {
    is_ipc_content_ = true;
    ready_          = false;
    pending_html_   = html;
    webview_->Navigate(L"webview://localhost/");
}

// load_file: navigate directly to the file:// URL.
// IPC transport uses webview://, and the scheme registration sets AllowedOrigins
// to "*", so cross-origin fetch() from file:// to webview://ipc/* is permitted.
void WebViewImpl::load_file(const std::string& path) {
    is_ipc_content_ = true;
    ready_          = false;
    pending_html_.clear();
    std::string url = "file:///" + path;
    // Normalise backslashes.
    for (auto& c : url) if (c == '\\') c = '/';
    webview_->Navigate(to_wide(url).c_str());
}

void WebViewImpl::reload()     { webview_->Reload(); }
void WebViewImpl::go_back()    { webview_->GoBack(); }
void WebViewImpl::go_forward() { webview_->GoForward(); }

std::string WebViewImpl::get_url() {
    CoStr url; webview_->get_Source(&url.ptr);
    return url.str();
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