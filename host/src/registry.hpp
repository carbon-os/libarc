#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <ui/ui.h>
#include <webview/webview.hpp>

namespace arc {

// View-backed webview: one ui::View + one webview::WebView, keyed by a single
// ID assigned by Go (used for both the view and its webview).
struct ManagedView {
    std::string                       id;
    int                               z_order = 0; // stacking order; higher = on top
    std::unique_ptr<ui::View>         view;
    std::unique_ptr<webview::WebView> webview;
};

// A window and all the surfaces it owns.
// Members are ordered so C++ destructs webviews before views before the window.
struct ManagedWindow {
    std::string                       id;
    std::string                       webview_id;  // ID of the window-backed WebView;
                                                    // empty if none created yet
    std::unique_ptr<ui::Window>       win;
    std::unique_ptr<webview::WebView> webview;     // window-backed; at most one; null until created
    std::vector<ManagedView>          views;       // view-backed overlays; destroyed before win
};

// ─────────────────────────────────────────────────────────────────────────────
// WindowRegistry
//
// Owns all live managed objects. All objects are keyed by string IDs assigned
// by Go. The structure is flat — there is no nesting beyond one level.
// ─────────────────────────────────────────────────────────────────────────────
class WindowRegistry {
public:
    // ── Windows ───────────────────────────────────────────────────────────────
    ManagedWindow* create_window(std::string id, ui::WindowConfig cfg);
    ManagedWindow* get_window(const std::string& id);
    bool           destroy_window(const std::string& id);

    // ── WebViews ──────────────────────────────────────────────────────────────

    // Window-backed webview. Returns nullptr if the window doesn't exist or
    // already has a window-backed webview (only one is allowed per window).
    webview::WebView* create_window_webview(const std::string& wv_id,
                                             const std::string& win_id,
                                             webview::WebViewConfig cfg);

    // View-backed webview (floating overlay). Any number per window.
    webview::WebView* create_view_webview(const std::string& view_id,
                                          const std::string& win_id,
                                          ui::ViewConfig     view_cfg,
                                          webview::WebViewConfig wv_cfg);

    // Look up any webview (window-backed or view-backed) by its ID.
    webview::WebView* get_webview(const std::string& id);

    // Returns non-null only for view-backed webviews.
    ManagedView* get_view(const std::string& id);

    bool destroy_webview(const std::string& id);

    // ── Z-order ───────────────────────────────────────────────────────────────

    // Update the z_order field on the named view then re-sort and restack all
    // overlay views on the owning window so the change takes effect immediately.
    // No-op if view_id doesn't refer to a view-backed webview.
    void set_view_zorder(const std::string& view_id, int z);

private:
    // Re-sort mw->views by z_order ascending and call bring_to_front() on each
    // in order so the native view hierarchy matches.  Lowest z = bottom,
    // highest z = top (painted last on macOS / Win32).
    void reorder_views(ManagedWindow& mw);

    std::unordered_map<std::string, ManagedWindow> windows_;

    // Flat reverse lookup: webview/view id → owning window id.
    std::unordered_map<std::string, std::string> webview_to_window_;
};

} // namespace arc