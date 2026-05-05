#include "WebViewImpl.hpp"
#include <gio/gio.h>

namespace webview {

void WebViewImpl::load_url(const std::string& url) {
    pending_html_.clear();
    resource_root_.clear();
    ready_ = false;
    webkit_web_view_load_uri(webkit_webview_, url.c_str());
}

void WebViewImpl::load_html(const std::string& html) {
    ready_         = false;
    pending_html_  = html;
    webkit_web_view_load_uri(webkit_webview_, "webview://app/");
}

void WebViewImpl::load_file(const std::string& path) {
    ready_        = false;
    pending_html_.clear();

    // Derive resource root from the file's containing directory so that
    // sibling assets (/assets/…) are resolved and served correctly.
    auto slash = path.rfind('/');
    resource_root_ = (slash != std::string::npos) ? path.substr(0, slash) : ".";

    // Read the entry-point file into pending_html_ — the scheme handler
    // serves it when webview://app/ (i.e. path "/") is requested, and
    // falls through to resource_root_ file serving for all other paths.
    GError*      err    = nullptr;
    GMappedFile* mapped = g_mapped_file_new(path.c_str(), FALSE, &err);
    if (mapped) {
        gsize       sz   = g_mapped_file_get_length(mapped);
        const char* data = g_mapped_file_get_contents(mapped);
        pending_html_ = std::string(data, sz);
        g_mapped_file_unref(mapped);
    } else {
        g_error_free(err);
    }

    webkit_web_view_load_uri(webkit_webview_, "webview://app/");
}

void WebViewImpl::reload()     { webkit_web_view_reload(webkit_webview_); }
void WebViewImpl::go_back()    { webkit_web_view_go_back(webkit_webview_); }
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