#include "WebViewImpl.hpp"
#include "string_util.hpp"
#include <webview/webview.hpp>

namespace webview {

using namespace detail;
using namespace Microsoft::WRL;

static ComPtr<ICoreWebView2CookieManager> get_cookie_manager(WebViewImpl* impl) {
    ComPtr<ICoreWebView2_2> wv2;
    if (FAILED(impl->webview_.As(&wv2))) return nullptr;
    ComPtr<ICoreWebView2CookieManager> mgr;
    wv2->get_CookieManager(&mgr);
    return mgr;
}

static Cookie wv2_cookie_to_cookie(ICoreWebView2Cookie* c) {
    Cookie out;
    CoStr name;  c->get_Name(&name.ptr);   out.name   = name.str();
    CoStr value; c->get_Value(&value.ptr); out.value  = value.str();
    CoStr domain;c->get_Domain(&domain.ptr);out.domain = domain.str();
    CoStr path;  c->get_Path(&path.ptr);   out.path   = path.str();

    BOOL secure = FALSE;   c->get_IsSecure(&secure);     out.secure    = (secure != FALSE);
    BOOL http   = FALSE;   c->get_IsHttpOnly(&http);     out.http_only = (http   != FALSE);

    double expires = 0;
    c->get_Expires(&expires);
    if (expires > 0) out.expires = static_cast<int64_t>(expires);
    return out;
}

void WebViewImpl::get_cookies(const std::string& url,
                              std::function<void(std::vector<Cookie>)> fn)
{
    auto mgr = get_cookie_manager(this);
    if (!mgr) { if (fn) fn({}); return; }

    mgr->GetCookies(
        to_wide(url).c_str(),
        Callback<ICoreWebView2GetCookiesCompletedHandler>(
            [fn = std::move(fn)](HRESULT,
                                 ICoreWebView2CookieList* list) -> HRESULT {
                std::vector<Cookie> result;
                if (list) {
                    UINT count = 0; list->get_Count(&count);
                    for (UINT i = 0; i < count; ++i) {
                        ComPtr<ICoreWebView2Cookie> c;
                        if (SUCCEEDED(list->GetValueAtIndex(i, &c)))
                            result.push_back(wv2_cookie_to_cookie(c.Get()));
                    }
                }
                if (fn) fn(std::move(result));
                return S_OK;
            }).Get());
}

void WebViewImpl::set_cookie(Cookie cookie, std::function<void(bool)> fn) {
    auto mgr = get_cookie_manager(this);
    if (!mgr) { if (fn) fn(false); return; }

    ComPtr<ICoreWebView2Cookie> c;
    mgr->CreateCookie(
        to_wide(cookie.name).c_str(),
        to_wide(cookie.value).c_str(),
        to_wide(cookie.domain).c_str(),
        to_wide(cookie.path).c_str(),
        &c);

    if (!c) { if (fn) fn(false); return; }

    c->put_IsSecure(cookie.secure ? TRUE : FALSE);
    c->put_IsHttpOnly(cookie.http_only ? TRUE : FALSE);
    if (cookie.expires)
        c->put_Expires(static_cast<double>(*cookie.expires));

    HRESULT hr = mgr->AddOrUpdateCookie(c.Get());
    if (fn) fn(SUCCEEDED(hr));
}

void WebViewImpl::delete_cookie(const std::string& name, const std::string& url,
                                std::function<void(bool)> fn)
{
    auto mgr = get_cookie_manager(this);
    if (!mgr) { if (fn) fn(false); return; }

    mgr->GetCookies(
        to_wide(url).c_str(),
        Callback<ICoreWebView2GetCookiesCompletedHandler>(
            [mgr, name, fn = std::move(fn)](
                HRESULT, ICoreWebView2CookieList* list) -> HRESULT {
                bool found = false;
                if (list) {
                    UINT count = 0; list->get_Count(&count);
                    for (UINT i = 0; i < count; ++i) {
                        ComPtr<ICoreWebView2Cookie> c;
                        if (SUCCEEDED(list->GetValueAtIndex(i, &c))) {
                            CoStr n; c->get_Name(&n.ptr);
                            if (n.str() == name) {
                                mgr->DeleteCookie(c.Get());
                                found = true;
                            }
                        }
                    }
                }
                if (fn) fn(found);
                return S_OK;
            }).Get());
}

void WebViewImpl::clear_cookies() {
    auto mgr = get_cookie_manager(this);
    if (mgr) mgr->DeleteAllCookies();
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