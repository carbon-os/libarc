# libui API Reference

> **Purpose:** libui is the windowing foundation that libwebview is built on top of. It owns
> everything above the web surface — window lifecycle, geometry, state, and appearance — and
> exposes a `NativeHandle` that libwebview accepts to parent itself inside. If you are embedding
> libwebview, you are expected to create and manage your windows and views through libui first,
> then hand the handle across.

> **Scope notice:** libui is strictly focused on window and view lifecycle, geometry, state, and
> appearance. It does not manage widgets, menus, or any rendered UI content.

---

## ui::Window

### Constructor

| Signature | Description |
|---|---|
| `Window(WindowConfig config)` | Creates and shows a new top-level window |

### WindowConfig

| Field | Type | Default | Description |
|---|---|---|---|
| `title` | `std::string` | `""` | Initial window title |
| `size` | `Size` | `{800, 600}` | Initial width and height in logical pixels |
| `min_size` | `std::optional<Size>` | `nullopt` | Minimum resizable size |
| `max_size` | `std::optional<Size>` | `nullopt` | Maximum resizable size |
| `position` | `std::optional<Point>` | `nullopt` | Initial position. `nullopt` lets the OS decide |
| `resizable` | `bool` | `true` | Whether the window can be resized |
| `style` | `WindowStyle` | `WindowStyle::Default` | Controls title bar and border decoration |
| `transparent` | `bool` | `false` | Transparent window background |
| `always_on_top` | `bool` | `false` | Float above other windows |
| `effect` | `std::optional<BackdropEffect>` | `nullopt` | Platform backdrop blur effect |

### ui::WindowStyle

| Value | Title Bar | Border | Description |
|---|---|---|---|
| `Default` | ✓ | ✓ | Full native decoration |
| `BorderOnly` | — | ✓ | Border and resize handle, no title bar. Useful for custom web-rendered title bars |
| `Borderless` | — | — | Completely undecorated window. Useful for splash screens or fully custom chrome |

---

## ui::Window — Geometry

| Method | Returns | Description |
|---|---|---|
| `set_size(Size size)` | `void` | Resize the window in logical pixels |
| `get_size()` | `Size` | Returns current window size |
| `set_position(Point point)` | `void` | Move the window |
| `get_position()` | `Point` | Returns current window position |
| `center()` | `void` | Center the window on its current screen |
| `set_min_size(Size size)` | `void` | Set minimum resizable size |
| `set_max_size(Size size)` | `void` | Set maximum resizable size |

---

## ui::Window — State

| Method | Returns | Description |
|---|---|---|
| `show()` | `void` | Make the window visible |
| `hide()` | `void` | Hide the window without destroying it |
| `focus()` | `void` | Bring the window to front and focus it |
| `minimize()` | `void` | Minimize to taskbar or dock |
| `maximize()` | `void` | Maximize to fill the screen |
| `restore()` | `void` | Restore from minimized or maximized |
| `run()` | `void` | Block until the application terminates |
| `set_fullscreen(bool)` | `void` | Enter or exit fullscreen |
| `is_fullscreen()` | `bool` | Returns whether the window is fullscreen |
| `is_visible()` | `bool` | Returns whether the window is visible |
| `is_focused()` | `bool` | Returns whether the window has focus |
| `get_state()` | `WindowState` | Returns the current `WindowState` |

### ui::WindowState

| Value | Description |
|---|---|
| `Normal` | Default windowed state |
| `Minimized` | Collapsed to taskbar or dock |
| `Maximized` | Expanded to fill available screen area |
| `Fullscreen` | Occupies the entire screen |

---

## ui::Window — Appearance

| Method | Returns | Description |
|---|---|---|
| `set_title(std::string title)` | `void` | Set the window title |
| `get_title()` | `std::string` | Returns the current window title |
| `set_always_on_top(bool)` | `void` | Toggle float above other windows |
| `set_effect(BackdropEffect)` | `void` | Apply a platform backdrop effect |
| `clear_effect()` | `void` | Remove any active backdrop effect |

### ui::BackdropEffect

| Value | Platform | Description |
|---|---|---|
| `Vibrancy` | macOS | NSVisualEffectView blur behind the window |
| `Acrylic` | Windows | Acrylic blur-behind material |
| `Mica` | Windows | Mica tinted system backdrop |
| `MicaAlt` | Windows | High-contrast Mica variant |

> **Platform note:** `BackdropEffect` requires `transparent: true` in `WindowConfig` to be
> visible. On unsupported platforms the call is a no-op.

---

## ui::Window — Events

| Method | Callback Signature | Description |
|---|---|---|
| `on_resize(fn)` | `(Size) -> void` | Fired when the window is resized |
| `on_move(fn)` | `(Point) -> void` | Fired when the window is moved |
| `on_focus(fn)` | `() -> void` | Fired when the window gains focus |
| `on_blur(fn)` | `() -> void` | Fired when the window loses focus |
| `on_state_change(fn)` | `(WindowState) -> void` | Fired when window state changes |
| `on_close(fn)` | `() -> bool` | Fired when the user requests close. Return `false` to cancel |
| `on_drop(fn)` | `(DropEvent&) -> void` | Fired when files are dropped onto the window |

---

## ui::Window — NativeHandle

| Method | Returns | Description |
|---|---|---|
| `native_handle()` | `NativeHandle` | Returns the platform handle for this window |

---

## ui::View

A fixed overlay surface parented to a window. Sits at a specified position and size inside the
parent window without participating in any layout — analogous to `position: fixed` in CSS.
Mirrors the `Window` API but scoped to what is meaningful for an inner surface: no fullscreen,
no minimize, no maximize, no screen-level positioning, no title.

Intended for hosting a secondary libwebview surface alongside a primary one inside the same window.

### Constructor

| Signature | Description |
|---|---|
| `View(Window& parent, ViewConfig config)` | Creates a view parented to an existing window |

### ViewConfig

| Field | Type | Default | Description |
|---|---|---|---|
| `size` | `Size` | `{0, 0}` | Initial size in logical pixels |
| `position` | `Point` | `{0, 0}` | Initial position within the parent window |
| `min_size` | `std::optional<Size>` | `nullopt` | Minimum resizable size |
| `max_size` | `std::optional<Size>` | `nullopt` | Maximum resizable size |
| `transparent` | `bool` | `false` | Transparent view background |
| `effect` | `std::optional<BackdropEffect>` | `nullopt` | Platform backdrop blur effect |

---

## ui::View — Geometry

| Method | Returns | Description |
|---|---|---|
| `set_size(Size size)` | `void` | Resize the view in logical pixels |
| `get_size()` | `Size` | Returns current view size |
| `set_position(Point point)` | `void` | Move the view within the parent window |
| `get_position()` | `Point` | Returns current position within the parent window |
| `set_min_size(Size size)` | `void` | Set minimum resizable size |
| `set_max_size(Size size)` | `void` | Set maximum resizable size |

---

## ui::View — State

| Method | Returns | Description |
|---|---|---|
| `show()` | `void` | Make the view visible |
| `hide()` | `void` | Hide the view without destroying it |
| `focus()` | `void` | Focus the view |
| `is_visible()` | `bool` | Returns whether the view is visible |
| `is_focused()` | `bool` | Returns whether the view has focus |

---

## ui::View — Stacking

| Method | Returns | Description |
|---|---|---|
| `bring_to_front()` | `void` | Re-inserts this view above all siblings in the parent window's subview stack |
| `send_to_back()` | `void` | Re-inserts this view below all siblings |

---

## ui::View — Appearance

| Method | Returns | Description |
|---|---|---|
| `set_effect(BackdropEffect)` | `void` | Apply a platform backdrop effect |
| `clear_effect()` | `void` | Remove any active backdrop effect |

---

## ui::View — Events

| Method | Callback Signature | Description |
|---|---|---|
| `on_resize(fn)` | `(Size) -> void` | Fired when the view is resized |
| `on_move(fn)` | `(Point) -> void` | Fired when the view is moved within the parent window |
| `on_focus(fn)` | `() -> void` | Fired when the view gains focus |
| `on_blur(fn)` | `() -> void` | Fired when the view loses focus |

---

## ui::View — NativeHandle

| Method | Returns | Description |
|---|---|---|
| `native_handle()` | `NativeHandle` | Returns the platform handle for this view |

---

## ui::PixelView

A fixed overlay surface parented to a window, like `ui::View`, but instead of hosting a native
web surface it polls a shared memory channel for raw pixel data and composites it directly to
screen. Useful for rendering local VM displays, camera feeds, or any source that produces raw
frames.

### Constructor

| Signature | Description |
|---|---|
| `PixelView(Window& parent, PixelViewConfig config)` | Creates a pixel view parented to an existing window and begins polling the named channel |

### PixelViewConfig

| Field | Type | Default | Description |
|---|---|---|---|
| `channel_id` | `std::string` | — | Shared memory channel identifier. Must match the producer's channel ID |
| `size` | `Size` | `{0, 0}` | Initial size in logical pixels |
| `position` | `Point` | `{0, 0}` | Initial position within the parent window |
| `min_size` | `std::optional<Size>` | `nullopt` | Minimum resizable size |
| `max_size` | `std::optional<Size>` | `nullopt` | Maximum resizable size |
| `format` | `PixelFormat` | `PixelFormat::BGRA8` | Expected pixel format of incoming frames |
| `poll_interval_ms` | `int` | `16` | How often to check the channel for a new frame (~60fps default) |
| `stretch` | `bool` | `true` | Stretch frames to fill the view, preserving aspect ratio |

### ui::PixelFormat

| Value | Description |
|---|---|
| `BGRA8` | 8-bit per channel, blue-green-red-alpha. Common on Windows |
| `RGBA8` | 8-bit per channel, red-green-blue-alpha |
| `RGB8` | 8-bit per channel, no alpha |
| `YUV420` | Planar YUV, common for video and VM display output |

---

## ui::PixelView — Geometry

| Method | Returns | Description |
|---|---|---|
| `set_size(Size size)` | `void` | Resize the view in logical pixels |
| `get_size()` | `Size` | Returns current view size |
| `set_position(Point point)` | `void` | Move the view within the parent window |
| `get_position()` | `Point` | Returns current position within the parent window |
| `set_min_size(Size size)` | `void` | Set minimum resizable size |
| `set_max_size(Size size)` | `void` | Set maximum resizable size |

---

## ui::PixelView — State

| Method | Returns | Description |
|---|---|---|
| `show()` | `void` | Make the view visible |
| `hide()` | `void` | Hide the view without destroying it |
| `is_visible()` | `bool` | Returns whether the view is visible |
| `is_connected()` | `bool` | Returns whether the shm channel is currently active and a producer is writing to it |
| `get_frame_count()` | `uint64_t` | Total number of frames rendered since the channel connected |

---

## ui::PixelView — Events

| Method | Callback Signature | Description |
|---|---|---|
| `on_resize(fn)` | `(Size) -> void` | Fired when the view is resized |
| `on_move(fn)` | `(Point) -> void` | Fired when the view is moved within the parent window |
| `on_connect(fn)` | `() -> void` | Fired when a producer attaches to the channel |
| `on_disconnect(fn)` | `() -> void` | Fired when the producer detaches or the channel goes silent |
| `on_frame(fn)` | `(FrameEvent&) -> void` | Fired each time a new frame is composited |

### ui::FrameEvent

| Member | Type | Description |
|---|---|---|
| `width` | `int` | Frame width in pixels |
| `height` | `int` | Frame height in pixels |
| `format` | `PixelFormat` | Pixel format of this frame |
| `frame_count` | `uint64_t` | Running frame count |

---

## ui::PixelView — NativeHandle

| Method | Returns | Description |
|---|---|---|
| `native_handle()` | `NativeHandle` | Returns the platform handle for this view |

---

## Pixel Channel Protocol

`ui::PixelView` consumes frames from a POSIX shared memory region written by an external
producer. The protocol is defined in `pixel_channel.h`.

The producer opens a shared memory region named `/ui_pv_<channel_id>` and maps at least
`sizeof(PixelChannelHeader) + data_size` bytes. It writes all header fields and pixel data, then
increments `frame_count` **last** so the consumer can use it as a seqlock-style dirty flag.
`PixelView` polls on its configured interval and re-renders whenever the observed `frame_count`
changes.

### ui::PixelChannelHeader

| Field | Type | Description |
|---|---|---|
| `frame_count` | `uint64_t` | Updated last by the producer; the consumer detects new frames by watching this value |
| `magic` | `uint32_t` | Must equal `kPixelChannelMagic` (`0x55495056`, i.e. `'UIPV'`) |
| `version` | `uint32_t` | Must equal `kPixelChannelVersion` (`1`) |
| `width` | `uint32_t` | Frame width in pixels |
| `height` | `uint32_t` | Frame height in pixels |
| `format` | `uint32_t` | `ui::PixelFormat` cast to `uint32_t` |
| `data_size` | `uint32_t` | Byte size of the pixel data that follows this header |

> `PixelChannelHeader` is always exactly 32 bytes (`static_assert` enforced).

### Constants and Helpers

| Symbol | Value / Signature | Description |
|---|---|---|
| `kPixelChannelMagic` | `0x55495056` | Magic number (`'UIPV'`) that must appear in every header |
| `kPixelChannelVersion` | `1` | Protocol version that must appear in every header |
| `pixel_channel_shm_name(channel_id)` | `std::string` | Returns the shm region name for a given channel ID: `/ui_pv_<channel_id>` |

---

## ui::DropEvent

| Member | Type | Description |
|---|---|---|
| `paths` | `std::vector<std::string>` | Absolute paths of dropped files |
| `position` | `Point` | Drop position relative to the window |

---

## Geometry Types

### ui::Size

| Field | Type |
|---|---|
| `width` | `int` |
| `height` | `int` |

### ui::Point

| Field | Type |
|---|---|
| `x` | `int` |
| `y` | `int` |

---

## ui::NativeHandle

Wraps a raw platform handle with a type tag. Returned by `native_handle()` on `Window`,
`View`, and `PixelView`.

| Method | Returns | Description |
|---|---|---|
| `get()` | `void*` | Returns the raw platform handle |
| `ptr()` | `void*` | Alias for `get()` |
| `type()` | `NativeHandleType` | Returns the handle type tag |
| `is_window()` | `bool` | `true` for `NSWindow`, `HWND`, `GtkWindow` |
| `is_view()` | `bool` | `true` for `NSView`, `GtkWidget` |

### ui::NativeHandleType

| Value | Platform |
|---|---|
| `NSWindow` | macOS |
| `NSView` | macOS |
| `HWND` | Windows |
| `GtkWindow` | Linux |
| `GtkWidget` | Linux |

---

## Platform Internals

| Macro / Symbol | Description |
|---|---|
| `UI_PLATFORM` | String identifying the current platform: `"linux"`, `"windows"`, or `"macos"` |
| `UI_FEATURE_BACKDROP_EFFECT` | Defined when the platform supports backdrop blur effects |