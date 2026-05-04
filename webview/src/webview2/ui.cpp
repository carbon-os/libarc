#include "WebViewImpl.hpp"
#include "string_util.hpp"
#include <webview/webview.hpp>

namespace webview {

using namespace detail;

// ── Find in page ──────────────────────────────────────────────────────────────
// Implemented via JavaScript using the browser's native window.find() API.
// WebView2's native ICoreWebView2_9::Find is not used here to avoid a hard
// dependency on a specific SDK version.

void WebViewImpl::find(const std::string& query, FindOptions opts,
                       std::function<void(FindResult)> fn)
{
    find_query_ = query;

    // window.find(str, caseSensitive, backwards, wrap)
    std::string js =
        "window.find(" +
        json(query).dump() + "," +
        (opts.case_sensitive ? "true" : "false") +
        ",false," +
        (opts.wrap ? "true" : "false") +
        ")";

    eval(js, [fn = std::move(fn)](EvalResult r) {
        if (fn) {
            FindResult result;
            result.match_count  = (r.ok() && r.value == true) ? 1 : 0;
            result.active_match = 0;
            fn(result);
        }
    });
}

void WebViewImpl::find_next() {
    if (find_query_.empty()) return;
    std::string js = "window.find(" + json(find_query_).dump() + ",false,false,true)";
    eval_js_raw(js);
}

void WebViewImpl::find_prev() {
    if (find_query_.empty()) return;
    std::string js = "window.find(" + json(find_query_).dump() + ",false,true,true)";
    eval_js_raw(js);
}

void WebViewImpl::stop_find() {
    find_query_.clear();
    // Collapse selection to clear the find highlight.
    eval_js_raw("window.getSelection().removeAllRanges()");
}

void WebView::find(std::string query, FindOptions opts,
                   std::function<void(FindResult)> fn)
    { impl_->find(query, opts, std::move(fn)); }
void WebView::find_next() { impl_->find_next(); }
void WebView::find_prev() { impl_->find_prev(); }
void WebView::stop_find() { impl_->stop_find(); }

// ── Zoom ──────────────────────────────────────────────────────────────────────

void WebViewImpl::set_zoom(double factor) {
    controller_->put_ZoomFactor(factor);
}

double WebViewImpl::get_zoom() {
    double z = 1.0;
    controller_->get_ZoomFactor(&z);
    return z;
}

void WebView::set_zoom(double factor) { impl_->set_zoom(factor); }
double WebView::get_zoom()            { return impl_->get_zoom(); }

// ── User agent ────────────────────────────────────────────────────────────────

void WebViewImpl::set_user_agent(const std::string& ua) {
    Microsoft::WRL::ComPtr<ICoreWebView2Settings2> s2;
    if (SUCCEEDED(settings_.As(&s2)))
        s2->put_UserAgent(to_wide(ua).c_str());
}

std::string WebViewImpl::get_user_agent() {
    Microsoft::WRL::ComPtr<ICoreWebView2Settings2> s2;
    if (SUCCEEDED(settings_.As(&s2))) {
        CoStr ua; s2->get_UserAgent(&ua.ptr);
        return ua.str();
    }
    return {};
}

void WebView::set_user_agent(std::string ua) { impl_->set_user_agent(ua); }
std::string WebView::get_user_agent()         { return impl_->get_user_agent(); }

// ── Cache ─────────────────────────────────────────────────────────────────────

void WebViewImpl::clear_cache() {
    Microsoft::WRL::ComPtr<ICoreWebView2_2> wv2;
    if (FAILED(webview_.As(&wv2))) return;
    Microsoft::WRL::ComPtr<ICoreWebView2Profile> profile;
    // ICoreWebView2_13 exposes Profile; fall back to clearing via CDP.
    // Use DevTools Protocol to clear cache storage.
    webview_->CallDevToolsProtocolMethod(
        L"Network.clearBrowserCache", L"{}", nullptr);
}

void WebView::clear_cache() { impl_->clear_cache(); }

} // namespace webview