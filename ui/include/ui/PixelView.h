#pragma once

#include "types.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace ui {

class Window;

struct PixelViewConfig {
    std::string            channel_id;
    Size                   size             = {0, 0};
    Point                  position         = {0, 0};
    std::optional<Size>    min_size         = std::nullopt;
    std::optional<Size>    max_size         = std::nullopt;
    PixelFormat            format           = PixelFormat::BGRA8;
    int                    poll_interval_ms = 16;
    bool                   stretch          = true;
};

class PixelView {
public:
    PixelView(Window& parent, PixelViewConfig config);
    ~PixelView();

    PixelView(const PixelView&)            = delete;
    PixelView& operator=(const PixelView&) = delete;

    // ── Geometry ──────────────────────────────────────────────────────────────
    void  set_size(Size size);
    Size  get_size()          const;
    void  set_position(Point point);
    Point get_position()      const;
    void  set_min_size(Size size);
    void  set_max_size(Size size);

    // ── State ─────────────────────────────────────────────────────────────────
    void     show();
    void     hide();
    bool     is_visible()    const;
    bool     is_connected()  const;
    uint64_t get_frame_count() const;

    // ── Events ────────────────────────────────────────────────────────────────
    void on_resize    (std::function<void(Size)>        fn);
    void on_move      (std::function<void(Point)>       fn);
    void on_connect   (std::function<void()>            fn);
    void on_disconnect(std::function<void()>            fn);
    void on_frame     (std::function<void(FrameEvent&)> fn);

    // ── NativeHandle ──────────────────────────────────────────────────────────
    NativeHandle native_handle() const;

    struct Impl;
private:
    std::unique_ptr<Impl> impl_;
};

} // namespace ui