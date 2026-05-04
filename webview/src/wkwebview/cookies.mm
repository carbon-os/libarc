#import "WebViewImpl.hpp"
#include <webview/webview.hpp>

namespace webview {

static Cookie ns_cookie_to_cookie(NSHTTPCookie* c) {
    Cookie out;
    out.name      = c.name.UTF8String  ?: "";
    out.value     = c.value.UTF8String ?: "";
    out.domain    = c.domain.UTF8String?: "";
    out.path      = c.path.UTF8String  ?: "/";
    out.secure    = c.isSecure;
    out.http_only = c.isHTTPOnly;
    if (c.expiresDate)
        out.expires = (int64_t)c.expiresDate.timeIntervalSince1970;
    return out;
}

void WebViewImpl::get_cookies(const std::string& url,
                              std::function<void(std::vector<Cookie>)> fn) {
    NSURL* nsUrl = [NSURL URLWithString:@(url.c_str())];
    [wkwebview_.configuration.websiteDataStore.httpCookieStore
        getAllCookies:^(NSArray<NSHTTPCookie*>* cookies) {
            std::vector<Cookie> result;
            for (NSHTTPCookie* c in cookies) {
                // Filter by URL host if provided
                if (nsUrl && c.domain.length &&
                    ![[nsUrl host] hasSuffix:c.domain]) continue;
                result.push_back(ns_cookie_to_cookie(c));
            }
            if (fn) fn(std::move(result));
        }];
}

void WebViewImpl::set_cookie(Cookie cookie, std::function<void(bool)> fn) {
    NSMutableDictionary* props = [NSMutableDictionary dictionary];
    props[NSHTTPCookieName]   = @(cookie.name.c_str());
    props[NSHTTPCookieValue]  = @(cookie.value.c_str());
    props[NSHTTPCookieDomain] = @(cookie.domain.c_str());
    props[NSHTTPCookiePath]   = @(cookie.path.c_str());
    if (cookie.secure)    props[NSHTTPCookieSecure]   = @YES;
    if (cookie.expires)   props[NSHTTPCookieExpires]  =
        [NSDate dateWithTimeIntervalSince1970:(NSTimeInterval)*cookie.expires];

    NSHTTPCookie* c = [NSHTTPCookie cookieWithProperties:props];
    if (!c) { if (fn) fn(false); return; }

    [wkwebview_.configuration.websiteDataStore.httpCookieStore
        setCookie:c completionHandler:^{
            if (fn) fn(true);
        }];
}

void WebViewImpl::delete_cookie(const std::string& name, const std::string& url,
                                std::function<void(bool)> fn) {
    [wkwebview_.configuration.websiteDataStore.httpCookieStore
        getAllCookies:^(NSArray<NSHTTPCookie*>* cookies) {
            NSString* nsName = @(name.c_str());
            __block BOOL found = NO;
            dispatch_group_t g = dispatch_group_create();
            for (NSHTTPCookie* c in cookies) {
                if (![c.name isEqualToString:nsName]) continue;
                found = YES;
                dispatch_group_enter(g);
                [wkwebview_.configuration.websiteDataStore.httpCookieStore
                    deleteCookie:c completionHandler:^{ dispatch_group_leave(g); }];
            }
            dispatch_group_notify(g, dispatch_get_main_queue(), ^{
                if (fn) fn((bool)found);
            });
        }];
}

void WebViewImpl::clear_cookies() {
    WKWebsiteDataStore* store = wkwebview_.configuration.websiteDataStore;
    NSSet* types = [NSSet setWithObject:WKWebsiteDataTypeCookies];
    [store removeDataOfTypes:types
              modifiedSince:[NSDate distantPast]
          completionHandler:^{}];
}

// ── Public WebView forwarders ─────────────────────────────────────────────────

void WebView::get_cookies(std::string url,
                          std::function<void(std::vector<Cookie>)> fn)
    { impl_->get_cookies(url, std::move(fn)); }

void WebView::set_cookie(Cookie cookie, std::function<void(bool)> fn)
    { impl_->set_cookie(std::move(cookie), std::move(fn)); }

void WebView::delete_cookie(std::string name, std::string url,
                            std::function<void(bool)> fn)
    { impl_->delete_cookie(name, url, std::move(fn)); }

void WebView::clear_cookies()
    { impl_->clear_cookies(); }

} // namespace webview