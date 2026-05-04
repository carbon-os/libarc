#include "WebViewImpl.hpp"

namespace webview {

void WebViewImpl::get_cookies(const std::string& url, std::function<void(std::vector<Cookie>)> fn) {
    if (fn) fn({}); // WebKitGTK sync cookie retrieval requires async SoupMessage iteration
}

void WebViewImpl::set_cookie(Cookie cookie, std::function<void(bool)> fn) {
    if (fn) fn(false);
}

void WebViewImpl::delete_cookie(const std::string& name, const std::string& url, std::function<void(bool)> fn) {
    if (fn) fn(false);
}

void WebViewImpl::clear_cookies() {
    WebKitWebsiteDataManager* manager = webkit_web_context_get_website_data_manager(web_context_);
    webkit_website_data_manager_clear(manager, WEBKIT_WEBSITE_DATA_COOKIES, 0, nullptr, nullptr, nullptr);
}

// ── Public WebView forwarders ─────────────────────────────────────────────────

void WebView::get_cookies(std::string url, std::function<void(std::vector<Cookie>)> fn)
    { impl_->get_cookies(url, std::move(fn)); }

void WebView::set_cookie(Cookie cookie, std::function<void(bool)> fn)
    { impl_->set_cookie(std::move(cookie), std::move(fn)); }

void WebView::delete_cookie(std::string name, std::string url, std::function<void(bool)> fn)
    { impl_->delete_cookie(name, url, std::move(fn)); }

void WebView::clear_cookies()
    { impl_->clear_cookies(); }

} // namespace webview