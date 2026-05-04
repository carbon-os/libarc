#pragma once

#include "types.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace ui {

struct WindowConfig {
    std::string            title        = "";
    Size                   size         = {800, 600};
    std::optional<Size>    min_size     = std::nullopt;
    std::optional<Size>    max_size     = std::nullopt;
    std::optional<Point>   position     = std::nullopt;
    bool                   resizable    = true;
    WindowStyle            style        = WindowStyle::Default;
    bool                   transparent  = false;
    bool                   always_on_top = false;
    std::optional<BackdropEffect> effect = std::nullopt;
};

class Window {
public:
    explicit Window(WindowConfig config);
    ~Window();

    Window(const Window&)            = delete;
    Window& operator=(const Window&) = delete;

    // ── Geometry ──────────────────────────────────────────────────────────────
    void  set_size(Size size);
    Size  get_size()          const;
    void  set_position(Point point);
    Point get_position()      const;
    void  center();
    void  set_min_size(Size size);
    void  set_max_size(Size size);

    // ── State ─────────────────────────────────────────────────────────────────
    void        show();
    void        hide();
    void        focus();
    void        minimize();
    void        maximize();
    void        restore();
    void        run();           // blocks until the app terminates
    void        set_fullscreen(bool fullscreen);
    bool        is_fullscreen()  const;
    bool        is_visible()     const;
    bool        is_focused()     const;
    WindowState get_state()      const;

    // ── Appearance ────────────────────────────────────────────────────────────
    void        set_title(std::string title);
    std::string get_title()         const;
    void        set_always_on_top(bool on_top);
    void        set_effect(BackdropEffect effect);
    void        clear_effect();

    // ── Events ────────────────────────────────────────────────────────────────
    void on_resize      (std::function<void(Size)>        fn);
    void on_move        (std::function<void(Point)>       fn);
    void on_focus       (std::function<void()>            fn);
    void on_blur        (std::function<void()>            fn);
    void on_state_change(std::function<void(WindowState)> fn);
    void on_close       (std::function<bool()>            fn);
    void on_drop        (std::function<void(DropEvent&)>  fn);

    // ── NativeHandle ──────────────────────────────────────────────────────────
    NativeHandle native_handle() const;

    struct Impl;
private:
    std::unique_ptr<Impl> impl_;
};

} // namespace ui