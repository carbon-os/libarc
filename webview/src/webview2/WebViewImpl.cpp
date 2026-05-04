#include "WebViewImpl.hpp"
#include "string_util.hpp"

#include <webview/webview.hpp>
#include <WebView2EnvironmentOptions.h>
#include <commctrl.h>
#include <shlwapi.h>

#include <stdexcept>

namespace webview {

using namespace detail;
using namespace Microsoft::WRL;

// ── Custom scheme registration ────────────────────────────────────────────────
// Registers "webview://" as a secure custom scheme that accepts requests from
// any origin, enabling the IPC transport and HTML content serving to work
// regardless of the origin of the loaded page.

class WebviewSchemeRegistration final
    : public RuntimeClass<RuntimeClassFlags<ClassicCom>,
                          ICoreWebView2CustomSchemeRegistration>
{
    static LPWSTR co_dup(const wchar_t* s) {
        size_t n = wcslen(s) + 1;
        auto* p = static_cast<LPWSTR>(CoTaskMemAlloc(n * sizeof(wchar_t)));
        if (p) wmemcpy(p, s, n);
        return p;
    }
public:
    HRESULT STDMETHODCALLTYPE get_SchemeName(LPWSTR* v) noexcept override {
        if (!v) return E_POINTER;
        *v = co_dup(L"webview");
        return *v ? S_OK : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE get_TreatAsSecure(BOOL* v) noexcept override {
        if (!v) return E_POINTER; *v = TRUE; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_TreatAsSecure(BOOL) noexcept override { return S_OK; }

    HRESULT STDMETHODCALLTYPE GetAllowedOrigins(
        UINT32* count, LPWSTR** origins) noexcept override
    {
        if (!count || !origins) return E_POINTER;
        *count   = 1;
        *origins = static_cast<LPWSTR*>(CoTaskMemAlloc(sizeof(LPWSTR)));
        if (!*origins) return E_OUTOFMEMORY;
        (*origins)[0] = co_dup(L"*");
        if (!(*origins)[0]) { CoTaskMemFree(*origins); return E_OUTOFMEMORY; }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetAllowedOrigins(UINT32, LPCWSTR*) noexcept override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_HasAuthorityComponent(BOOL* v) noexcept override {
        if (!v) return E_POINTER; *v = TRUE; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_HasAuthorityComponent(BOOL) noexcept override {
        return S_OK;
    }
};

// ── Parent-window subclass proc (auto-resize) ─────────────────────────────────

static LRESULT CALLBACK ParentSubclassProc(
    HWND hWnd, UINT msg, WPARAM wp, LPARAM lp,
    UINT_PTR /*id*/, DWORD_PTR data)
{
    if (msg == WM_SIZE) {
        auto* impl = reinterpret_cast<WebViewImpl*>(data);
        if (impl) impl->resize_to_parent();
    }
    return DefSubclassProc(hWnd, msg, wp, lp);
}

// ── Constructor / Destructor ──────────────────────────────────────────────────

WebViewImpl::WebViewImpl(NativeHandle handle, WebViewConfig config) {
    devtools_enabled_ = config.devtools;
    setup(handle, config);
}

WebViewImpl::~WebViewImpl() {
    if (webview_) {
        webview_->remove_NavigationStarting(nav_starting_token_);
        webview_->remove_NavigationCompleted(nav_completed_token_);
        webview_->remove_ContentLoading(content_loading_token_);
        webview_->remove_DocumentTitleChanged(title_changed_token_);
        webview_->remove_NewWindowRequested(new_window_token_);
        webview_->remove_WindowCloseRequested(close_token_);
        webview_->remove_WebResourceRequested(web_resource_token_);
        webview_->remove_PermissionRequested(permission_token_);
    }
    if (hwnd_) RemoveWindowSubclass(hwnd_, ParentSubclassProc, 1);
    if (controller_) controller_->Close();
}

// ── Setup ─────────────────────────────────────────────────────────────────────

void WebViewImpl::setup(NativeHandle handle, WebViewConfig config) {
    // Resolve parent HWND from NativeHandle.
    // Both NSWindow-equivalent (top-level) and NSView-equivalent (child HWND)
    // are accepted; WebView2 hosts inside any valid HWND.
    switch (handle.type()) {
        case NativeHandleType::HWND:
            hwnd_ = static_cast<HWND>(handle.get());
            break;
        default:
            // On Windows only HWND is meaningful.
            return;
    }

    // Build environment options with custom scheme registration.
    auto opts = Make<CoreWebView2EnvironmentOptions>();
    ComPtr<ICoreWebView2EnvironmentOptions4> opts4;
    if (SUCCEEDED(opts.As(&opts4))) {
        auto reg = Make<WebviewSchemeRegistration>();
        ICoreWebView2CustomSchemeRegistration* regs[] = { reg.Get() };
        opts4->SetCustomSchemeRegistrations(1, regs);
    }

    // Convert paths to wide strings
    std::wstring browser_dir = config.webview2_runtime_path.empty()
        ? L"" : to_wide(config.webview2_runtime_path);
    std::wstring user_dir = config.webview2_user_data_path.empty()
        ? L"" : to_wide(config.webview2_user_data_path);

    LPCWSTR pBrowserDir = browser_dir.empty() ? nullptr : browser_dir.c_str();
    LPCWSTR pUserDir    = user_dir.empty()    ? nullptr : user_dir.c_str();

    // Async creation — pump the message loop until both callbacks fire.
    bool created = false;

    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        pBrowserDir, pUserDir, opts.Get(),
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this, &created](HRESULT res, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(res) || !env) { created = true; return res; }
                environment_ = env;

                return env->CreateCoreWebView2Controller(hwnd_,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this, &created](HRESULT res2,
                                         ICoreWebView2Controller* ctrl) -> HRESULT {
                            if (FAILED(res2) || !ctrl) { created = true; return res2; }
                            controller_ = ctrl;
                            controller_->get_CoreWebView2(&webview_);
                            webview_->get_Settings(&settings_);

                            if (devtools_enabled_)
                                settings_->put_AreDevToolsEnabled(TRUE);

                            // Register webview:// filter for IPC + content serving.
                            webview_->AddWebResourceRequestedFilter(
                                L"webview://*",
                                COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);

                            install_ipc_bridge();
                            wire_events();
                            resize_to_parent();
                            created = true;
                            return S_OK;
                        }).Get());
            }).Get());

    if (FAILED(hr)) return;

    // Pump until created.
    MSG msg;
    while (!created) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        } else {
            Sleep(1);
        }
    }

    // Auto-resize when the parent window is resized.
    SetWindowSubclass(hwnd_, ParentSubclassProc, 1,
                      reinterpret_cast<DWORD_PTR>(this));
}

// ── Event wiring ──────────────────────────────────────────────────────────────

void WebViewImpl::wire_events() {

    // NavigationStarting → on_navigate_cb
    webview_->add_NavigationStarting(
        Callback<ICoreWebView2NavigationStartingEventHandler>(
            [this](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) {
                CoStr url; args->get_Uri(&url.ptr);
                if (on_navigate_cb) {
                    NavigationEvent ev;
                    ev.url = url.str();
                    on_navigate_cb(ev);
                    if (ev.is_cancelled()) args->put_Cancel(TRUE);
                }
                return S_OK;
            }).Get(), &nav_starting_token_);

    // ContentLoading → on_load_commit_cb
    webview_->add_ContentLoading(
        Callback<ICoreWebView2ContentLoadingEventHandler>(
            [this](ICoreWebView2* wv, ICoreWebView2ContentLoadingEventArgs*) {
                if (on_load_commit_cb) {
                    CoStr url; wv->get_Source(&url.ptr);
                    LoadEvent ev; ev.url = url.str(); ev.is_main_frame = true;
                    on_load_commit_cb(ev);
                }
                return S_OK;
            }).Get(), &content_loading_token_);

    // NavigationCompleted → on_load_finish_cb / on_load_failed_cb / on_ready_cb
    webview_->add_NavigationCompleted(
        Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [this](ICoreWebView2* wv, ICoreWebView2NavigationCompletedEventArgs* args) {
                BOOL success = FALSE; args->get_IsSuccess(&success);
                CoStr url; wv->get_Source(&url.ptr);
                std::string urlStr = url.str();

                if (success) {
                    if (!ready_) {
                        ready_ = true;
                        if (on_ready_cb) on_ready_cb();
                    }
                    if (on_load_finish_cb) {
                        LoadEvent ev; ev.url = urlStr; ev.is_main_frame = true;
                        on_load_finish_cb(ev);
                    }
                } else {
                    COREWEBVIEW2_WEB_ERROR_STATUS status{};
                    args->get_WebErrorStatus(&status);
                    if (on_load_failed_cb) {
                        LoadFailedEvent ev;
                        ev.url = urlStr; ev.is_main_frame = true;
                        ev.error_code = static_cast<int>(status);
                        ev.error_description = "Navigation failed";
                        on_load_failed_cb(ev);
                    }
                }
                return S_OK;
            }).Get(), &nav_completed_token_);

    // DocumentTitleChanged → on_title_change_cb
    webview_->add_DocumentTitleChanged(
        Callback<ICoreWebView2DocumentTitleChangedEventHandler>(
            [this](ICoreWebView2* wv, IUnknown*) {
                if (on_title_change_cb) {
                    CoStr title; wv->get_DocumentTitle(&title.ptr);
                    on_title_change_cb(title.str());
                }
                return S_OK;
            }).Get(), &title_changed_token_);

    // NewWindowRequested → on_new_window_cb
    webview_->add_NewWindowRequested(
        Callback<ICoreWebView2NewWindowRequestedEventHandler>(
            [this](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) {
                args->put_Handled(TRUE);
                CoStr url; args->get_Uri(&url.ptr);
                BOOL gesture = FALSE; args->get_IsUserInitiated(&gesture);

                if (on_new_window_cb) {
                    NewWindowEvent ev;
                    ev.url = url.str();
                    ev.is_user_gesture = (gesture == TRUE);
                    on_new_window_cb(ev);
                    if (!ev.redirect_url().empty())
                        webview_->Navigate(to_wide(ev.redirect_url()).c_str());
                }
                return S_OK;
            }).Get(), &new_window_token_);

    // WindowCloseRequested → on_close_cb
    webview_->add_WindowCloseRequested(
        Callback<ICoreWebView2WindowCloseRequestedEventHandler>(
            [this](ICoreWebView2*, IUnknown*) {
                if (on_close_cb) on_close_cb();
                return S_OK;
            }).Get(), &close_token_);

    // WebResourceRequested → dispatch_web_resource_request
    webview_->add_WebResourceRequested(
        Callback<ICoreWebView2WebResourceRequestedEventHandler>(
            [this](ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs* args) {
                dispatch_web_resource_request(args);
                return S_OK;
            }).Get(), &web_resource_token_);

    // PermissionRequested → on_permission_cb
    webview_->add_PermissionRequested(
        Callback<ICoreWebView2PermissionRequestedEventHandler>(
            [this](ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs* args) {
                COREWEBVIEW2_PERMISSION_KIND kind{};
                args->get_PermissionKind(&kind);
                CoStr origin; args->get_Uri(&origin.ptr);

                PermissionType type{};
                bool handled = true;
                switch (kind) {
                    case COREWEBVIEW2_PERMISSION_KIND_CAMERA:
                        type = PermissionType::Camera; break;
                    case COREWEBVIEW2_PERMISSION_KIND_MICROPHONE:
                        type = PermissionType::Microphone; break;
                    case COREWEBVIEW2_PERMISSION_KIND_GEOLOCATION:
                        type = PermissionType::Geolocation; break;
                    case COREWEBVIEW2_PERMISSION_KIND_NOTIFICATIONS:
                        type = PermissionType::Notifications; break;
                    case COREWEBVIEW2_PERMISSION_KIND_CLIPBOARD_READ:
                        type = PermissionType::ClipboardRead; break;
                    default:
                        handled = false; break;
                }

                if (!handled || !on_permission_cb) {
                    args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY);
                    return S_OK;
                }

                PermissionRequest req;
                req.permission = type;
                req.origin = origin.str();
                on_permission_cb(req);
                args->put_State(req.is_granted()
                    ? COREWEBVIEW2_PERMISSION_STATE_ALLOW
                    : COREWEBVIEW2_PERMISSION_STATE_DENY);
                return S_OK;
            }).Get(), &permission_token_);
}

// ── Helpers ───────────────────────────────────────────────────────────────────

void WebViewImpl::eval_js_raw(const std::string& js) {
    if (webview_) webview_->ExecuteScript(to_wide(js).c_str(), nullptr);
}

void WebViewImpl::resize_to_parent() {
    if (!controller_ || !hwnd_) return;
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    controller_->put_Bounds(rc);
}

// ── Public WebView constructor / destructor ───────────────────────────────────

WebView::WebView(NativeHandle handle, WebViewConfig config)
    : impl_(std::make_unique<WebViewImpl>(handle, config))
    , ipc(impl_.get())
{}

WebView::~WebView() = default;

} // namespace webview