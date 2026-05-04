#pragma once

#include <string>

#include <ipc/ipc.hpp>
#include <nlohmann/json.hpp>

#include "registry.hpp"

// BillingManager is only compiled on Apple and Windows.
#if defined(__APPLE__) || defined(_WIN32)
#  include "billing_manager.hpp"
#  include <memory>
#endif

namespace arc {

class CommandDispatcher {
public:
    CommandDispatcher(ipc::Server& server, WindowRegistry& registry);

    void dispatch(const ipc::Message& msg);

#if defined(__APPLE__) || defined(_WIN32)
    // Called by Host after constructing BillingManager; transfers ownership.
    void set_billing(std::unique_ptr<BillingManager> billing);
#endif

private:
    // ── Host ──────────────────────────────────────────────────────────────────
    void on_host_configure(const nlohmann::json& j);
    void on_host_ping();
    void on_host_shutdown();

    // ── Window ────────────────────────────────────────────────────────────────
    void on_window_create(const nlohmann::json& j);
    void on_window_destroy(const nlohmann::json& j);
    void on_window_set_title(const nlohmann::json& j);
    void on_window_set_size(const nlohmann::json& j);
    void on_window_set_position(const nlohmann::json& j);
    void on_window_center(const nlohmann::json& j);
    void on_window_show(const nlohmann::json& j);
    void on_window_hide(const nlohmann::json& j);
    void on_window_focus(const nlohmann::json& j);
    void on_window_minimize(const nlohmann::json& j);
    void on_window_maximize(const nlohmann::json& j);
    void on_window_restore(const nlohmann::json& j);
    void on_window_set_fullscreen(const nlohmann::json& j);
    void on_window_set_min_size(const nlohmann::json& j);
    void on_window_set_max_size(const nlohmann::json& j);
    void on_window_set_always_on_top(const nlohmann::json& j);
    void on_window_set_effect(const nlohmann::json& j);
    void on_window_clear_effect(const nlohmann::json& j);

    // ── WebView ───────────────────────────────────────────────────────────────
    void on_webview_create(const nlohmann::json& j);
    void on_webview_destroy(const nlohmann::json& j);
    void on_webview_load_url(const nlohmann::json& j);
    void on_webview_load_html(const nlohmann::json& j);
    void on_webview_load_file(const nlohmann::json& j);
    void on_webview_reload(const nlohmann::json& j);
    void on_webview_go_back(const nlohmann::json& j);
    void on_webview_go_forward(const nlohmann::json& j);
    void on_webview_eval(const nlohmann::json& j);
    void on_webview_set_zoom(const nlohmann::json& j);
    void on_webview_send_ipc(const nlohmann::json& j);
    void on_webview_set_position(const nlohmann::json& j);
    void on_webview_set_size(const nlohmann::json& j);
    void on_webview_show(const nlohmann::json& j);
    void on_webview_hide(const nlohmann::json& j);
    void on_webview_set_zorder(const nlohmann::json& j);

    // ── Wiring helpers ────────────────────────────────────────────────────────
    void wire_window_events(const std::string& id, ui::Window& win);
    void wire_webview_events(const std::string& id, webview::WebView& wv);

    static std::optional<ui::BackdropEffect> parse_effect(const std::string& name);

    void emit(nlohmann::json evt);

    ipc::Server&    server_;
    WindowRegistry& registry_;
    std::string     app_name_;

#if defined(__APPLE__) || defined(_WIN32)
    std::unique_ptr<BillingManager> billing_;
#endif
};

} // namespace arc