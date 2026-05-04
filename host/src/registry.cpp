#include "registry.hpp"
#include "native_handle_cast.hpp"

#include <algorithm>

namespace arc {

// ── Windows ───────────────────────────────────────────────────────────────────

ManagedWindow* WindowRegistry::create_window(std::string id, ui::WindowConfig cfg) {
    if (windows_.count(id)) return nullptr;

    ManagedWindow mw;
    mw.id  = id;
    mw.win = std::make_unique<ui::Window>(std::move(cfg));

    auto [it, ok] = windows_.emplace(std::move(id), std::move(mw));
    return ok ? &it->second : nullptr;
}

ManagedWindow* WindowRegistry::get_window(const std::string& id) {
    auto it = windows_.find(id);
    return it != windows_.end() ? &it->second : nullptr;
}

bool WindowRegistry::destroy_window(const std::string& id) {
    auto it = windows_.find(id);
    if (it == windows_.end()) return false;

    auto& mw = it->second;

    if (!mw.webview_id.empty())
        webview_to_window_.erase(mw.webview_id);
    for (auto& mv : mw.views)
        webview_to_window_.erase(mv.id);

    windows_.erase(it);
    return true;
}

// ── WebViews ──────────────────────────────────────────────────────────────────

webview::WebView* WindowRegistry::create_window_webview(const std::string& wv_id,
                                                         const std::string& win_id,
                                                         webview::WebViewConfig cfg) {
    auto* mw = get_window(win_id);
    if (!mw || mw->webview) return nullptr;

    auto wv = std::make_unique<webview::WebView>(
        to_webview_handle(mw->win->native_handle()), std::move(cfg));

    mw->webview    = std::move(wv);
    mw->webview_id = wv_id;
    webview_to_window_[wv_id] = win_id;
    return mw->webview.get();
}

webview::WebView* WindowRegistry::create_view_webview(const std::string& view_id,
                                                       const std::string& win_id,
                                                       ui::ViewConfig     view_cfg,
                                                       webview::WebViewConfig wv_cfg) {
    auto* mw = get_window(win_id);
    if (!mw) return nullptr;

    ManagedView mv;
    mv.id      = view_id;
    mv.z_order = 0; // caller sets via set_view_zorder after creation
    mv.view    = std::make_unique<ui::View>(*mw->win, std::move(view_cfg));
    mv.webview = std::make_unique<webview::WebView>(
        to_webview_handle(mv.view->native_handle()), std::move(wv_cfg));

    auto* raw = mv.webview.get();
    mw->views.push_back(std::move(mv));
    webview_to_window_[view_id] = win_id;
    return raw;
}

webview::WebView* WindowRegistry::get_webview(const std::string& id) {
    auto it = webview_to_window_.find(id);
    if (it == webview_to_window_.end()) return nullptr;

    auto* mw = get_window(it->second);
    if (!mw) return nullptr;

    if (mw->webview_id == id)
        return mw->webview.get();

    for (auto& mv : mw->views)
        if (mv.id == id) return mv.webview.get();

    return nullptr;
}

ManagedView* WindowRegistry::get_view(const std::string& id) {
    auto it = webview_to_window_.find(id);
    if (it == webview_to_window_.end()) return nullptr;

    auto* mw = get_window(it->second);
    if (!mw) return nullptr;

    for (auto& mv : mw->views)
        if (mv.id == id) return &mv;

    return nullptr;
}

bool WindowRegistry::destroy_webview(const std::string& id) {
    auto it = webview_to_window_.find(id);
    if (it == webview_to_window_.end()) return false;

    auto* mw = get_window(it->second);
    if (!mw) {
        webview_to_window_.erase(it);
        return false;
    }

    if (mw->webview_id == id) {
        mw->webview.reset();
        mw->webview_id.clear();
        webview_to_window_.erase(it);
        return true;
    }

    auto& views = mw->views;
    auto vit = std::find_if(views.begin(), views.end(),
                             [&id](const ManagedView& mv) { return mv.id == id; });
    if (vit != views.end()) {
        views.erase(vit);
        webview_to_window_.erase(it);
        return true;
    }

    return false;
}

// ── Z-order ───────────────────────────────────────────────────────────────────

void WindowRegistry::set_view_zorder(const std::string& view_id, int z) {
    auto it = webview_to_window_.find(view_id);
    if (it == webview_to_window_.end()) return;

    auto* mw = get_window(it->second);
    if (!mw) return;

    for (auto& mv : mw->views) {
        if (mv.id == view_id) {
            mv.z_order = z;
            break;
        }
    }

    reorder_views(*mw);
}

void WindowRegistry::reorder_views(ManagedWindow& mw) {
    // Stable sort so views with equal z_order keep their insertion order.
    std::stable_sort(mw.views.begin(), mw.views.end(),
                     [](const ManagedView& a, const ManagedView& b) {
                         return a.z_order < b.z_order;
                     });

    // Restack in ascending order: bring_to_front called last wins, so the
    // highest-z view ends up on top after the loop.
    for (auto& mv : mw.views)
        mv.view->bring_to_front();
}

} // namespace arc