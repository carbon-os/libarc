#include "WebViewImpl.hpp"

namespace webview {

void WebViewImpl::find(const std::string& query, FindOptions opts, std::function<void(FindResult)> fn) {
    guint options = WEBKIT_FIND_OPTIONS_NONE;
    if (!opts.case_sensitive) options |= WEBKIT_FIND_OPTIONS_CASE_INSENSITIVE;
    if (opts.wrap)            options |= WEBKIT_FIND_OPTIONS_WRAP_AROUND;

    webkit_find_controller_search(find_controller_, query.c_str(), options, G_MAXUINT);
    
    if (fn) {
        FindResult r; // WebKitGTK requires connecting to "found-text" signals for counts.
        r.match_count = 0;
        r.active_match = 0;
        fn(r);
    }
}

void WebViewImpl::find_next() { webkit_find_controller_search_next(find_controller_); }
void WebViewImpl::find_prev() { webkit_find_controller_search_previous(find_controller_); }
void WebViewImpl::stop_find() { webkit_find_controller_search_finish(find_controller_); }

void WebViewImpl::set_zoom(double factor) { webkit_web_view_set_zoom_level(webkit_webview_, factor); }
double WebViewImpl::get_zoom() { return webkit_web_view_get_zoom_level(webkit_webview_); }

void WebViewImpl::set_user_agent(const std::string& ua) {
    WebKitSettings* settings = webkit_web_view_get_settings(webkit_webview_);
    webkit_settings_set_user_agent(settings, ua.c_str());
}

std::string WebViewImpl::get_user_agent() {
    WebKitSettings* settings = webkit_web_view_get_settings(webkit_webview_);
    const gchar* ua = webkit_settings_get_user_agent(settings);
    return ua ? ua : "";
}

void WebViewImpl::clear_cache() {
    webkit_web_context_clear_cache(web_context_);
}

// ── Public WebView forwarders ─────────────────────────────────────────────────

void WebView::find(std::string query, FindOptions opts, std::function<void(FindResult)> fn)
    { impl_->find(query, opts, std::move(fn)); }

void WebView::find_next() { impl_->find_next(); }
void WebView::find_prev() { impl_->find_prev(); }
void WebView::stop_find() { impl_->stop_find(); }

void WebView::set_zoom(double factor) { impl_->set_zoom(factor); }
double WebView::get_zoom()            { return impl_->get_zoom(); }

void WebView::set_user_agent(std::string ua) { impl_->set_user_agent(ua); }
std::string WebView::get_user_agent()         { return impl_->get_user_agent(); }

void WebView::clear_cache() { impl_->clear_cache(); }

} // namespace webview