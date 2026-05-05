#pragma once

#include "platform.hpp"
#include "native_handle.hpp"
#include "events.hpp"
#include "auth.hpp"
#include "permission.hpp"
#include "download.hpp"
#include "find.hpp"
#include "cookie.hpp"
#include "request.hpp"
#include "dialog.hpp"
#include "script.hpp"
#include "ipc.hpp"

#include <string>
#include <vector>
#include <optional>
#include <functional>
#include <memory>

namespace webview {

struct WebViewConfig {
    bool        devtools      = false;
    std::string resource_root;               // absolute path, no trailing slash
    std::string webview2_user_data_path;
    std::string webview2_runtime_path;
};

class WebViewImpl; // platform-private

class WebView {
    std::unique_ptr<WebViewImpl> impl_;

public:
    WebView(NativeHandle handle, WebViewConfig config = {});
    ~WebView();

    WebView(const WebView&)            = delete;
    WebView& operator=(const WebView&) = delete;

    // ── Navigation ──────────────────────────────────────────────────────────
    void        load_url(std::string url);
    void        load_html(std::string html);
    void        load_file(std::string path);
    void        reload();
    void        go_back();
    void        go_forward();
    std::string get_url();

    // ── Resource root ────────────────────────────────────────────────────────
    void set_resource_root(std::string path);

    // ── Lifecycle events ────────────────────────────────────────────────────
    void on_ready(std::function<void()> fn);
    void on_close(std::function<bool()> fn);
    void on_navigate(std::function<void(NavigationEvent&)> fn);
    void on_title_change(std::function<void(std::string)> fn);

    // ── Page load events ────────────────────────────────────────────────────
    void on_load_start(std::function<void(LoadEvent&)> fn);
    void on_load_commit(std::function<void(LoadEvent&)> fn);
    void on_load_finish(std::function<void(LoadEvent&)> fn);
    void on_load_failed(std::function<void(LoadFailedEvent&)> fn);

    // ── Permissions ─────────────────────────────────────────────────────────
    void on_permission_request(std::function<void(PermissionRequest&)> fn);

    // ── Downloads ───────────────────────────────────────────────────────────
    void on_download_start(std::function<bool(DownloadEvent&)> fn);
    void on_download_progress(std::function<void(DownloadEvent&)> fn);
    void on_download_complete(std::function<void(DownloadEvent&)> fn);

    // ── New window ──────────────────────────────────────────────────────────
    void on_new_window(std::function<void(NewWindowEvent&)> fn);

    // ── Auth challenges ─────────────────────────────────────────────────────
    void on_auth_challenge(std::function<void(AuthChallenge&)> fn);

    // ── Console messages ────────────────────────────────────────────────────
    void on_console_message(std::function<void(ConsoleMessage&)> fn);

    // ── Script ──────────────────────────────────────────────────────────────
    void eval(std::string js, std::function<void(EvalResult)> fn);
    void add_user_script(std::string js, ScriptInjectTime time);
    void remove_user_scripts();

    // ── Find in page ────────────────────────────────────────────────────────
    void find(std::string query, FindOptions opts, std::function<void(FindResult)> fn);
    void find_next();
    void find_prev();
    void stop_find();

    // ── Zoom ────────────────────────────────────────────────────────────────
    void   set_zoom(double factor);
    double get_zoom();

    // ── User agent ──────────────────────────────────────────────────────────
    void        set_user_agent(std::string ua);
    std::string get_user_agent();

    // ── Cache ────────────────────────────────────────────────────────────────
    void clear_cache();

    // ── Cookies ─────────────────────────────────────────────────────────────
    void get_cookies(std::string url, std::function<void(std::vector<Cookie>)> fn);
    void set_cookie(Cookie cookie, std::function<void(bool)> fn);
    void delete_cookie(std::string name, std::string url, std::function<void(bool)> fn);
    void clear_cookies();

    // ── Request interception ─────────────────────────────────────────────────
    void on_request(std::function<void(ResourceRequest&)> fn);

    // ── File dialog ──────────────────────────────────────────────────────────
    std::optional<std::string>  dialog(FileDialog d);
    std::vector<std::string>    dialog_multi(FileDialog d);

    // ── IPC ──────────────────────────────────────────────────────────────────
    IPC ipc;
};

} // namespace webview