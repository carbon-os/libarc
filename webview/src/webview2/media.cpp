#include "WebViewImpl.hpp"
#include "string_util.hpp"
#include <webview/webview.hpp>
#include <sstream>

// Downloads require ICoreWebView2_4 (WebView2 SDK 1.0.902.49+).

namespace webview {

using namespace detail;
using namespace Microsoft::WRL;

static void register_download_handler(WebViewImpl* impl) {
    ComPtr<ICoreWebView2_4> wv4;
    if (FAILED(impl->webview_.As(&wv4))) return;

    wv4->add_DownloadStarting(
        Callback<ICoreWebView2DownloadStartingEventHandler>(
            [impl](ICoreWebView2*, ICoreWebView2DownloadStartingEventArgs* args)
            -> HRESULT
        {
            ComPtr<ICoreWebView2DownloadOperation> op;
            args->get_DownloadOperation(&op);

            // Build a stable string ID from the object pointer.
            std::ostringstream oss;
            oss << reinterpret_cast<uintptr_t>(op.Get());
            std::string did = oss.str();

            CoStr url_cs; op->get_Uri(&url_cs.ptr);
            
            // WebView2 provides the suggested filename via the default result path.
            CoStr fname;  
            args->get_ResultFilePath(&fname.ptr);
            std::string full_path = fname.str();
            std::string suggested_name = full_path;
            
            // Extract just the filename from the path
            auto pos = suggested_name.find_last_of("\\/");
            if (pos != std::string::npos) {
                suggested_name = suggested_name.substr(pos + 1);
            }

            INT64 total = -1; op->get_TotalBytesToReceive(&total);

            DownloadEvent ev;
            ev.id                 = did;
            ev.url                = url_cs.str();
            ev.suggested_filename = suggested_name;
            ev.total_bytes        = total;

            bool allow = true;
            if (impl->on_download_start_cb)
                allow = impl->on_download_start_cb(ev);

            if (!allow || ev.is_cancelled()) {
                args->put_Cancel(TRUE);
                return S_OK;
            }

            if (!ev.destination.empty())
                args->put_ResultFilePath(to_wide(ev.destination).c_str());

            impl->active_downloads_[did] = ev;

            // Progress
            if (impl->on_download_progress_cb) {
                op->add_BytesReceivedChanged(
                    Callback<ICoreWebView2BytesReceivedChangedEventHandler>(
                        [impl, did](ICoreWebView2DownloadOperation* op2, IUnknown*)
                        -> HRESULT
                    {
                        auto it = impl->active_downloads_.find(did);
                        if (it == impl->active_downloads_.end()) return S_OK;
                        INT64 recv = 0; op2->get_BytesReceived(&recv);
                        it->second.bytes_received = recv;
                        impl->on_download_progress_cb(it->second);
                        return S_OK;
                    }).Get(), nullptr);
            }

            // Completion
            op->add_StateChanged(
                Callback<ICoreWebView2StateChangedEventHandler>(
                    [impl, did](ICoreWebView2DownloadOperation* op2, IUnknown*)
                    -> HRESULT
                {
                    COREWEBVIEW2_DOWNLOAD_STATE state{};
                    op2->get_State(&state);
                    if (state == COREWEBVIEW2_DOWNLOAD_STATE_IN_PROGRESS) return S_OK;

                    auto it = impl->active_downloads_.find(did);
                    if (it == impl->active_downloads_.end()) return S_OK;

                    it->second.set_failed(
                        state == COREWEBVIEW2_DOWNLOAD_STATE_INTERRUPTED);

                    if (impl->on_download_complete_cb)
                        impl->on_download_complete_cb(it->second);

                    impl->active_downloads_.erase(it);
                    return S_OK;
                }).Get(), nullptr);

            return S_OK;
        }).Get(), &impl->download_token_);
}

// ── Auth challenge — ICoreWebView2_10 (SDK 1.0.1245.22+) ──────────────────────

static void register_auth_handler(WebViewImpl* impl) {
    ComPtr<ICoreWebView2_10> wv10;
    if (FAILED(impl->webview_.As(&wv10))) return;

    wv10->add_BasicAuthenticationRequested(
        Callback<ICoreWebView2BasicAuthenticationRequestedEventHandler>(
            [impl](ICoreWebView2*,
                   ICoreWebView2BasicAuthenticationRequestedEventArgs* args)
            -> HRESULT
        {
            if (!impl->on_auth_cb) {
                args->put_Cancel(TRUE);
                return S_OK;
            }

            ComPtr<ICoreWebView2BasicAuthenticationResponse> resp;
            args->get_Response(&resp);

            CoStr host_cs; args->get_Uri(&host_cs.ptr);
            CoStr realm_cs; args->get_Challenge(&realm_cs.ptr);

            AuthChallenge ch;
            ch.host     = host_cs.str();
            ch.realm    = realm_cs.str();
            ch.is_proxy = false;

            ComPtr<ICoreWebView2Deferral> deferral;
            args->GetDeferral(&deferral);

            impl->on_auth_cb(ch);

            switch (ch.action()) {
                case AuthChallenge::Action::Respond:
                    resp->put_UserName(to_wide(ch.user()).c_str());
                    resp->put_Password(to_wide(ch.password()).c_str());
                    break;
                default:
                    args->put_Cancel(TRUE);
                    break;
            }
            deferral->Complete();
            return S_OK;
        }).Get(), &impl->auth_token_);
}

// Called from wire_events() after the webview is created.
// Kept in media.cpp alongside download handling to mirror the macOS layout.
void webview_register_media_handlers(WebViewImpl* impl) {
    register_download_handler(impl);
    register_auth_handler(impl);
}

} // namespace webview