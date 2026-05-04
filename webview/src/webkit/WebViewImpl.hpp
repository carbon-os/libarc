#pragma once

#include <webview/webview.hpp>
#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

#include <string>
#include <functional>
#include <unordered_map>
#include <vector>
#include <optional>

namespace webview {

class WebViewImpl {
public:
    WebViewImpl(NativeHandle handle, WebViewConfig config);
    ~WebViewImpl();

    // ── Navigation ────────────────────────────────────────────────────────
    void        load_url(const std::string& url);
    void        load_html(const std::string& html);
    void        load_file(const std::string& path);
    void        reload();
    void        go_back();
    void        go_forward();
    std::string get_url();

    // ── Script ────────────────────────────────────────────────────────────
    void eval(const std::string& js, std::function<void(EvalResult)> fn);
    void add_user_script(const std::string& js, ScriptInjectTime time);
    void remove_user_scripts();

    // ── Find ──────────────────────────────────────────────────────────────
    void find(const std::string& query, FindOptions opts, std::function<void(FindResult)> fn);
    void find_next();
    void find_prev();
    void stop_find();

    // ── Zoom ──────────────────────────────────────────────────────────────
    void   set_zoom(double factor);
    double get_zoom();

    // ── User agent ────────────────────────────────────────────────────────
    void        set_user_agent(const std::string& ua);
    std::string get_user_agent();

    // ── Cache ─────────────────────────────────────────────────────────────
    void clear_cache();

    // ── Cookies ───────────────────────────────────────────────────────────
    void get_cookies(const std::string& url, std::function<void(std::vector<Cookie>)> fn);
    void set_cookie(Cookie cookie, std::function<void(bool)> fn);
    void delete_cookie(const std::string& name, const std::string& url, std::function<void(bool)> fn);
    void clear_cookies();

    // ── File dialog ───────────────────────────────────────────────────────
    std::optional<std::string> show_dialog(const FileDialog& d);
    std::vector<std::string>   show_dialog_multi(const FileDialog& d);

    // ── IPC internals ─────────────────────────────────────────────────────
    void ipc_handle(const std::string& ch, std::function<void(Message&)> fn);
    void ipc_on(const std::string& ch, std::function<void(Message&)> fn);
    void ipc_send(const std::string& ch, const json& body);
    void ipc_invoke(const std::string& ch, const json& body, std::function<void(Message&)> fn);

    void ipc_handle_binary(const std::string& ch, std::function<void(BinaryMessage&)> fn);
    void ipc_on_binary(const std::string& ch, std::function<void(BinaryMessage&)> fn);
    void ipc_send_binary(const std::string& ch, std::vector<uint8_t> data);
    void ipc_invoke_binary(const std::string& ch, std::vector<uint8_t> data, std::function<void(BinaryMessage&)> fn);

    void handle_scheme_ipc_task(WebKitURISchemeRequest* request);

    // ── Stored callbacks ──────────────────────────────────────────────────
    std::function<void()>                        on_ready_cb;
    std::function<bool()>                        on_close_cb;
    std::function<void(NavigationEvent&)>        on_navigate_cb;
    std::function<void(std::string)>             on_title_change_cb;

    std::function<void(LoadEvent&)>              on_load_start_cb;
    std::function<void(LoadEvent&)>              on_load_commit_cb;
    std::function<void(LoadEvent&)>              on_load_finish_cb;
    std::function<void(LoadFailedEvent&)>        on_load_failed_cb;

    std::function<void(PermissionRequest&)>      on_permission_cb;

    std::function<bool(DownloadEvent&)>          on_download_start_cb;
    std::function<void(DownloadEvent&)>          on_download_progress_cb;
    std::function<void(DownloadEvent&)>          on_download_complete_cb;

    std::function<void(NewWindowEvent&)>         on_new_window_cb;
    std::function<void(AuthChallenge&)>          on_auth_cb;
    std::function<void(ConsoleMessage&)>         on_console_cb;
    std::function<void(ResourceRequest&)>        on_request_cb;

    // ── State ─────────────────────────────────────────────────────────────
    bool ready_            = false;
    bool devtools_enabled_ = false;

    // ── Pending content (served through webview://app/) ───────────────────
    std::string pending_html_;
    std::string pending_file_path_;

    std::unordered_map<std::string, DownloadEvent> active_downloads_;

    // ── IPC channel tables ────────────────────────────────────────────────
    std::unordered_map<std::string, std::function<void(Message&)>>       ipc_handlers_;
    std::unordered_map<std::string, std::function<void(Message&)>>       ipc_listeners_;
    std::unordered_map<std::string, std::function<void(BinaryMessage&)>> ipc_bin_handlers_;
    std::unordered_map<std::string, std::function<void(BinaryMessage&)>> ipc_bin_listeners_;
    std::unordered_map<std::string, std::vector<uint8_t>>                pull_queue_;

    struct PendingHostInvoke       { std::function<void(Message&)>       fn; };
    struct PendingHostBinaryInvoke { std::function<void(BinaryMessage&)> fn; };
    std::unordered_map<std::string, PendingHostInvoke>       pending_host_invokes_;
    std::unordered_map<std::string, PendingHostBinaryInvoke> pending_host_bin_invokes_;

    WebKitWebView*            webkit_webview_  = nullptr;
    WebKitUserContentManager* ucm_             = nullptr;
    WebKitWebContext*         web_context_     = nullptr;
    WebKitFindController*     find_controller_ = nullptr;

private:
    void setup(NativeHandle handle, WebViewConfig config);
    void install_ipc_bridge();

public:
    void eval_js_raw(const std::string& js);

    static void     on_load_changed(WebKitWebView*, WebKitLoadEvent, gpointer);
    static void     on_title_changed(GObject*, GParamSpec*, gpointer);
    static gboolean on_decide_policy(WebKitWebView*, WebKitPolicyDecision*, WebKitPolicyDecisionType, gpointer);
    static void     on_script_message_received(WebKitUserContentManager*, WebKitJavascriptResult*, gpointer);
    static gboolean on_permission_request(WebKitWebView*, WebKitPermissionRequest*, gpointer);
    static void     on_download_started(WebKitWebContext*, WebKitDownload*, gpointer);
    static gboolean on_authenticate(WebKitWebView*, WebKitAuthenticationRequest*, gpointer);
    static void     on_uri_scheme_request(WebKitURISchemeRequest*, gpointer);
    static gboolean on_close_requested(WebKitWebView*, gpointer);
};

} // namespace webview