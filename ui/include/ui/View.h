#pragma once

#include "types.h"

#include <functional>
#include <memory>
#include <optional>

namespace ui {

class Window;

struct ViewConfig {
    Size                   size      = {0, 0};
    Point                  position  = {0, 0};
    std::optional<Size>    min_size  = std::nullopt;
    std::optional<Size>    max_size  = std::nullopt;
    bool                   transparent = false;
    std::optional<BackdropEffect> effect = std::nullopt;
};

class View {
public:
    View(Window& parent, ViewConfig config);
    ~View();

    View(const View&)            = delete;
    View& operator=(const View&) = delete;

    // ── Geometry ──────────────────────────────────────────────────────────────
    void  set_size(Size size);
    Size  get_size()          const;
    void  set_position(Point point);
    Point get_position()      const;
    void  set_min_size(Size size);
    void  set_max_size(Size size);

    // ── State ─────────────────────────────────────────────────────────────────
    void show();
    void hide();
    void focus();
    bool is_visible() const;
    bool is_focused() const;

    // ── Stacking ──────────────────────────────────────────────────────────────

    // bring_to_front re-inserts this view above all siblings in the parent
    // view's subview stack. Used by WindowRegistry::reorder_views to apply
    // z_order sorting.
    void bring_to_front();

    // send_to_back re-inserts this view below all siblings.
    void send_to_back();

    // ── Appearance ────────────────────────────────────────────────────────────
    void set_effect(BackdropEffect effect);
    void clear_effect();

    // ── Events ────────────────────────────────────────────────────────────────
    void on_resize(std::function<void(Size)>  fn);
    void on_move  (std::function<void(Point)> fn);
    void on_focus (std::function<void()>      fn);
    void on_blur  (std::function<void()>      fn);

    // ── NativeHandle ──────────────────────────────────────────────────────────
    NativeHandle native_handle() const;

    struct Impl;
private:
    std::unique_ptr<Impl> impl_;
};

} // namespace ui