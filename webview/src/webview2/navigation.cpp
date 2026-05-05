#include "WebViewImpl.hpp"
#include "string_util.hpp"
#include <webview/webview.hpp>
#include <fstream>

namespace webview {

using namespace detail;

void WebViewImpl::load_url(const std::string& url) {
    is_ipc_content_ = false;
    ready_          = false;
    pending_html_.clear();
    webview_->Navigate(to_wide(url).c_str());
}

void WebViewImpl::load_html(const std::string& html) {
    is_ipc_content_ = true;
    ready_          = false;
    pending_html_   = html;
    webview_->Navigate(L"webview://app/");
}

void WebViewImpl::load_file(const std::string& path) {
    is_ipc_content_ = true;
    ready_          = false;

    // Derive resource root from the file's containing directory so that
    // sibling assets are resolved and served correctly via webview://app/*.
    std::string norm = path;
    for (auto& c : norm) if (c == '/') c = '\\';
    auto slash = norm.rfind('\\');
    resource_root_ = (slash != std::string::npos) ? norm.substr(0, slash) : ".";

    // Read the file into pending_html_ so the scheme handler can serve it.
    std::ifstream f(path, std::ios::binary);
    pending_html_ = f ? std::string(std::istreambuf_iterator<char>(f), {}) : "";

    webview_->Navigate(L"webview://app/");
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