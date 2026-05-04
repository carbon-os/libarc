#include "WebViewImpl.hpp"
#include "string_util.hpp"
#include <webview/webview.hpp>

namespace webview {

using namespace detail;
using namespace Microsoft::WRL;

void WebViewImpl::eval(const std::string& js, std::function<void(EvalResult)> fn) {
    webview_->ExecuteScript(
        to_wide(js).c_str(),
        Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
            [fn = std::move(fn)](HRESULT hr, LPCWSTR result) -> HRESULT {
                EvalResult r;
                if (FAILED(hr)) {
                    r.error = "ExecuteScript failed";
                } else if (result) {
                    std::string s = to_utf8(result);
                    // WebView2 returns JSON-serialised values.
                    r.value = json::parse(s, nullptr, false);
                    if (r.value.is_discarded()) {
                        // Fallback: treat as raw string
                        r.value = s;
                    }
                }
                if (fn) fn(std::move(r));
                return S_OK;
            }).Get());
}

void WebViewImpl::add_user_script(const std::string& js, ScriptInjectTime time) {
    // DocumentStart scripts are injected before any page scripts.
    // WebView2 only supports at-document-start injection via
    // AddScriptToExecuteOnDocumentCreated; DocumentEnd is approximated by
    // deferring execution until DOMContentLoaded via a wrapper.

    std::wstring source = to_wide(js);

    if (time == ScriptInjectTime::DocumentEnd) {
        // Wrap in a DOMContentLoaded listener so it runs after the DOM is ready.
        source = LR"(document.addEventListener('DOMContentLoaded',function(){)" +
                 source + L"});";
    }

    bool done = false;
    webview_->AddScriptToExecuteOnDocumentCreated(
        source.c_str(),
        Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>(
            [this, &done](HRESULT, LPCWSTR id) -> HRESULT {
                if (id) user_script_ids_.emplace_back(id);
                done = true;
                return S_OK;
            }).Get());

    // Pump briefly to capture the ID before returning.
    MSG msg;
    for (int i = 0; i < 100 && !done; ++i) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        } else {
            Sleep(1);
        }
    }
}

void WebViewImpl::remove_user_scripts() {
    for (auto& id : user_script_ids_)
        webview_->RemoveScriptToExecuteOnDocumentCreated(id.c_str());
    user_script_ids_.clear();
}

// ── Public WebView forwarders ─────────────────────────────────────────────────

void WebView::eval(std::string js, std::function<void(EvalResult)> fn)
    { impl_->eval(js, std::move(fn)); }

void WebView::add_user_script(std::string js, ScriptInjectTime time)
    { impl_->add_user_script(js, time); }

void WebView::remove_user_scripts()
    { impl_->remove_user_scripts(); }

} // namespace webview