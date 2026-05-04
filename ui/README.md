# libui

A lightweight C++ windowing library for macOS, Windows, and Linux. libui owns window and view lifecycle, geometry, state, and appearance — and exposes a `NativeHandle` that can be passed to a web renderer (such as libwebview) to parent itself inside.

> **Scope:** libui handles windows and views only. It does not manage widgets, menus, or rendered UI content.

---

## Requirements

- CMake 3.21+
- C++17
- Xcode / Apple Clang (macOS), MSVC or Clang (Windows), GCC or Clang (Linux)

---

## Building

```sh
cmake -B build
cmake --build build
```

The library builds as a static archive (`libui.a`). Headers are installed under `include/ui/`.

---

## Concepts

### Window

A top-level native window. Created and shown immediately on construction.

```cpp
#include <ui/ui.h>

ui::Window window({
    .title     = "My App",
    .size      = {1280, 800},
    .style     = ui::WindowStyle::Default,
    .resizable = true,
});
```

Pass the window's `NativeHandle` to a web renderer or any other surface that needs a native parent:

```cpp
auto handle = window.native_handle(); // NativeHandleType::NSWindow on macOS
```

### View

A fixed overlay surface parented to a `Window`. Analogous to `position: fixed` in CSS — it sits at an explicit position and size inside the parent without participating in any layout. Intended for hosting a secondary web surface alongside a primary one.

```cpp
ui::View view(window, {
    .size     = {400, 300},
    .position = {100, 100},
});

auto handle = view.native_handle(); // NativeHandleType::NSView on macOS
```

### PixelView

Like `View`, but instead of hosting a native surface it polls a shared memory channel for raw pixel frames and composites them directly to screen. Useful for VM displays, camera feeds, or any source that produces raw pixel data.

```cpp
ui::PixelView pv(window, {
    .channel_id       = "my-feed",
    .size             = {1920, 1080},
    .format           = ui::PixelFormat::BGRA8,
    .poll_interval_ms = 16,  // ~60 fps
    .stretch          = true,
});

pv.on_connect([&]{ /* producer attached */ });
pv.on_frame([&](ui::FrameEvent& e) { /* new frame rendered */ });
```

---

## Shared Memory Protocol (PixelView)

A producer writes frames into a POSIX shared memory region named `/ui_pv_<channel_id>`. The layout is a `PixelChannelHeader` followed immediately by the pixel data.

```cpp
struct PixelChannelHeader {
    uint64_t frame_count; // increment LAST — used as a dirty flag
    uint32_t magic;       // must be kPixelChannelMagic   (0x55495056)
    uint32_t version;     // must be kPixelChannelVersion (1)
    uint32_t width;
    uint32_t height;
    uint32_t format;      // ui::PixelFormat cast to uint32_t
    uint32_t data_size;   // byte size of pixel data after the header
};
```

`frame_count` must be written **after** pixel data is fully committed so the consumer can use it as a seqlock-style dirty flag. `PixelView` re-renders whenever it observes a change in `frame_count`.

---

## Window Styles

| Style | Title Bar | Border | Use Case |
|---|---|---|---|
| `Default` | ✓ | ✓ | Standard native window |
| `BorderOnly` | — | ✓ | Custom web-rendered title bar |
| `Borderless` | — | — | Splash screens, fully custom chrome |

---

## Backdrop Effects

Platform blur effects can be applied to windows and views. `transparent: true` is required for the effect to be visible.

| Effect | Platform |
|---|---|
| `Vibrancy` | macOS |
| `Acrylic` | Windows |
| `Mica` / `MicaAlt` | Windows |

```cpp
ui::Window window({
    .transparent = true,
    .effect      = ui::BackdropEffect::Vibrancy,
});
```

Calls on unsupported platforms are no-ops.

---

## Events

```cpp
window.on_resize([](ui::Size s)       { /* ... */ });
window.on_move  ([](ui::Point p)      { /* ... */ });
window.on_focus ([]()                 { /* ... */ });
window.on_blur  ([]()                 { /* ... */ });
window.on_state_change([](ui::WindowState s) { /* ... */ });
window.on_drop  ([](ui::DropEvent& e) {
    for (auto& path : e.paths) { /* ... */ }
});

// Return false to prevent the window from closing
window.on_close([]() -> bool { return true; });
```

---

## Platform Support

| Platform | Status |
|---|---|
| macOS | ✅ Implemented |
| Windows | ✅ Implemented |
| Linux | ✅ Implemented |

Platform can be detected at compile time via:

```cpp
UI_PLATFORM              // "macos" | "windows" | "linux"
UI_FEATURE_BACKDROP_EFFECT  // defined when backdrop blur is supported
```

---

## API Reference

See [`reference.md`](reference.md) for the full API surface.