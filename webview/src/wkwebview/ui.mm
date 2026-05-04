#import "WebViewImpl.hpp"
#include <webview/webview.hpp>
#include <string>

namespace webview {

// ── Find in page — requires macOS 11+ ────────────────────────────────────────

void WebViewImpl::find(const std::string& query, FindOptions opts,
                       std::function<void(FindResult)> fn) {
    if (@available(macOS 11.0, *)) {
        WKFindConfiguration* cfg = [[WKFindConfiguration alloc] init];
        cfg.caseSensitive   = opts.case_sensitive;
        cfg.wraps           = opts.wrap;
        cfg.backwards       = NO;

        [wkwebview_ findString:@(query.c_str())
             withConfiguration:cfg
             completionHandler:^(WKFindResult* result) {
            FindResult r;
            r.match_count  = result.matchFound ? 1 : 0; // WKFindResult only gives matchFound
            r.active_match = 0;
            if (fn) fn(r);
        }];
    }
    // No-op on < macOS 11
}

void WebViewImpl::find_next() {
    // WKWebView public API doesn't expose next/prev directly;
    // trigger a new forward find on the same query via JS.
    eval_js_raw("window.getSelection().collapseToEnd();");
}

void WebViewImpl::find_prev() {
    // Similar limitation — exercise via JS selection manipulation.
    eval_js_raw("window.getSelection().collapseToStart();");
}

void WebViewImpl::stop_find() {
    if (@available(macOS 11.0, *)) {
        WKFindConfiguration* cfg = [[WKFindConfiguration alloc] init];
        [wkwebview_ findString:@"" withConfiguration:cfg
               completionHandler:^(WKFindResult*){}];
    }
}

void WebView::find(std::string query, FindOptions opts,
                   std::function<void(FindResult)> fn)
    { impl_->find(query, opts, std::move(fn)); }

void WebView::find_next() { impl_->find_next(); }
void WebView::find_prev() { impl_->find_prev(); }
void WebView::stop_find() { impl_->stop_find(); }

// ── Zoom ──────────────────────────────────────────────────────────────────────

void WebViewImpl::set_zoom(double factor) {
    if (@available(macOS 11.0, *)) {
        wkwebview_.pageZoom = factor;
    } else {
        wkwebview_.magnification = factor;
    }
}

double WebViewImpl::get_zoom() {
    if (@available(macOS 11.0, *)) {
        return wkwebview_.pageZoom;
    }
    return wkwebview_.magnification;
}

void WebView::set_zoom(double factor) { impl_->set_zoom(factor); }
double WebView::get_zoom()            { return impl_->get_zoom(); }

// ── User agent ────────────────────────────────────────────────────────────────

void WebViewImpl::set_user_agent(const std::string& ua) {
    wkwebview_.customUserAgent = @(ua.c_str());
}

std::string WebViewImpl::get_user_agent() {
    return wkwebview_.customUserAgent.UTF8String ?: "";
}

void WebView::set_user_agent(std::string ua) { impl_->set_user_agent(ua); }
std::string WebView::get_user_agent()         { return impl_->get_user_agent(); }

// ── Cache ─────────────────────────────────────────────────────────────────────

void WebViewImpl::clear_cache() {
    NSSet* types = [WKWebsiteDataStore allWebsiteDataTypes];
    // Narrow to only cache types, not cookies/storage
    types = [NSSet setWithObjects:
        WKWebsiteDataTypeDiskCache,
        WKWebsiteDataTypeMemoryCache,
        nil];
    [wkwebview_.configuration.websiteDataStore
        removeDataOfTypes:types
           modifiedSince:[NSDate distantPast]
       completionHandler:^{}];
}

void WebView::clear_cache() { impl_->clear_cache(); }

} // namespace webview