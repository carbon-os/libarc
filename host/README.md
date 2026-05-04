# libhost

The native host library for the Arc desktop runtime. `libhost` owns the
platform event loop, a window/webview registry, and a JSON-over-IPC command
dispatcher. A Go controller (or any other process) talks to it by exchanging
JSON messages over a named channel; `libhost` translates those messages into
native UI operations and emits events back.

---

## Architecture

```
┌─────────────────────────────────────────────────────┐
│                      libhost                        │
│                                                     │
│  ┌────────┐   main-thread   ┌────────────────────┐  │
│  │  IPC   │ ─────msgs─────▶ │ CommandDispatcher  │  │
│  │ Server │                 └────────┬───────────┘  │
│  │        │ ◀────events──── emit()  │               │
│  └────────┘                         │               │
│                             ┌───────▼────────────┐  │
│                             │  WindowRegistry    │  │
│                             │  ManagedWindow[]   │  │
│                             │    └ webview       │  │
│                             │    └ views[]       │  │
│                             └────────────────────┘  │
└─────────────────────────────────────────────────────┘
```

| Component | File(s) | Responsibility |
|---|---|---|
| `Host` | `host.hpp / host.cpp` | Entry point; owns everything, runs the event loop |
| `WindowRegistry` | `registry.hpp / registry.cpp` | Owns all `ManagedWindow` and `ManagedView` objects |
| `CommandDispatcher` | `dispatcher.hpp / dispatcher.cpp` | Routes incoming JSON to the right registry/UI call |
| Main-thread dispatch | `main_thread.hpp / main_thread.cpp` | Platform-specific trampoline (GCD / Win32 / GTK idle) |

---

## Host modes

`Host` supports two operating modes configured via `HostConfig::mode`.

### `HostMode::Managed`

The host process is launched as a subprocess by an external controller (e.g.
the Go layer). The controller supplies a `channel_id` on the command line; the
host listens on that channel and the controller connects as an `ipc::Client`.

```cpp
arc::Host host({
    .mode       = arc::HostMode::Managed,
    .channel_id = argv[1],   // passed in by the controller
});
host.run();                  // blocks until shutdown
```

### `HostMode::Embedded`

The host lives inside the same process as the controller. It generates its own
`channel_id`, registers an in-process transport, then `dlopen`s (or
`LoadLibrary`s on Windows) a module and calls its `AppMain` entry point on a
detached thread. The module connects back as an `ipc::Client` using the
provided channel ID.

```cpp
arc::Host host({
    .mode        = arc::HostMode::Embedded,
    .module_path = "/path/to/app.so",
    // channel_id is auto-generated when left empty
});
host.run();
```

The module must export:

```c
extern "C" void AppMain(const char* channel_id);
```

---

## Building

`libhost` is a static library consumed via CMake `add_subdirectory`. It depends
on three sibling targets and one vcpkg package:

| Dependency | Source | Notes |
|---|---|---|
| `ui` | sibling `add_subdirectory` | Native window/view abstraction |
| `webview` | sibling `add_subdirectory` | WebView abstraction |
| `ipc` | sibling `add_subdirectory` | IPC transport |
| `nlohmann_json` | vcpkg (`find_package`) | JSON parsing |

On **macOS**, `main_thread.cpp` is compiled as Objective-C++ (ARC enabled) and
links `Foundation` + `AppKit`.  
On **Linux**, GTK 3 is required and located via `pkg-config`.

```cmake
add_subdirectory(libs/host)
target_link_libraries(my_target PRIVATE host)
```

Requires C++20.

---

## IPC protocol

All messages are JSON objects with a `"type"` string field. The controller
sends *commands*; the host emits *events*.

### Lifecycle

| Direction | Message | Description |
|---|---|---|
| host → controller | `host.ready` | Emitted once the IPC server is listening |
| controller → host | `host.configure` | Set global app properties (`app_name`) |
| host → controller | `host.configured` | Acknowledgement |
| controller → host | `host.ping` | Liveness check |
| host → controller | `host.pong` | Liveness reply |
| controller → host | `host.shutdown` | Graceful shutdown |

### Window commands

All window commands carry an `"id"` field that identifies the target window.

| Command | Extra fields | Description |
|---|---|---|
| `window.create` | `id`, `title`, `width`, `height`, `resizable`, `style` | Create a new window |
| `window.destroy` | `id` | Destroy a window and all its surfaces |
| `window.set_title` | `id`, `title` | Update the title bar |
| `window.set_size` | `id`, `width`, `height` | Resize the window |
| `window.set_position` | `id`, `x`, `y` | Move the window |
| `window.center` | `id` | Center on screen |
| `window.show` / `window.hide` | `id` | Visibility |
| `window.focus` | `id` | Raise and focus |
| `window.minimize` / `window.maximize` / `window.restore` | `id` | Window state |
| `window.set_fullscreen` | `id`, `fullscreen` | Toggle fullscreen |
| `window.set_min_size` / `window.set_max_size` | `id`, `width`, `height` | Size constraints |
| `window.set_always_on_top` | `id`, `value` | Z-order hint |
| `window.set_effect` | `id`, `effect` | Backdrop effect (`vibrancy`, `acrylic`, `mica`, `mica_alt`) |
| `window.clear_effect` | `id` | Remove backdrop effect |

`style` values for `window.create`: `"default"`, `"border_only"`, `"borderless"`.

### Window events

| Event | Fields | Description |
|---|---|---|
| `window.resized` | `id`, `width`, `height` | Content area size changed |
| `window.moved` | `id`, `x`, `y` | Position changed |
| `window.focused` / `window.blurred` | `id` | Focus change |
| `window.state_changed` | `id`, `state` | `normal`, `minimized`, `maximized`, `fullscreen` |
| `window.closed` | `id` | User closed the window |

On `window.create` the host immediately emits one `window.resized` and one
`window.moved` event so the controller has accurate initial geometry.

### WebView commands

WebViews come in two modes set at creation time and cannot be changed
afterwards.

**`mode: "window"`** — the webview fills the entire window content area.  
**`mode: "view"`** — the webview sits in a floating `ui::View` overlay with
explicit position, size, and z-order.

| Command | Extra fields | Description |
|---|---|---|
| `webview.create` | `id`, `window_id`, `mode`, `devtools`, `x`\*, `y`\*, `width`\*, `height`\*, `z`\* | Create a webview (\*view mode only) |
| `webview.destroy` | `id` | Destroy a webview |
| `webview.load_url` | `id`, `url` | Navigate to URL |
| `webview.load_html` | `id`, `html` | Load raw HTML string |
| `webview.load_file` | `id`, `path` | Load a local file |
| `webview.reload` | `id` | Reload current page |
| `webview.go_back` / `webview.go_forward` | `id` | History navigation |
| `webview.eval` | `id`, `js` | Evaluate JavaScript (fire-and-forget) |
| `webview.set_zoom` | `id`, `zoom` | Set zoom factor (1.0 = 100 %) |
| `webview.send_ipc` | `id`, `channel`, `body` | Send a message into the page's IPC layer |
| `webview.set_position` | `id`, `x`, `y` | Move overlay (view mode only) |
| `webview.set_size` | `id`, `width`, `height` | Resize overlay (view mode only) |
| `webview.show` / `webview.hide` | `id` | Visibility (view mode only) |
| `webview.set_zorder` | `id`, `z` | Stacking order (view mode only; higher = on top) |

### WebView events

| Event | Fields | Description |
|---|---|---|
| `webview.ready` | `id` | WebView initialised and ready |
| `webview.load_start` | `id`, `url` | Navigation began |
| `webview.load_finish` | `id`, `url` | Page fully loaded |
| `webview.load_failed` | `id`, `url`, `error` | Navigation failed |
| `webview.navigate` | `id`, `url` | In-page navigation |
| `webview.title` | `id`, `title` | Page title changed |
| `webview.console` | `id`, `level`, `text` | Console message (`log`, `info`, `warn`, `error`) |
| `webview.ipc` | `id`, `channel`, `body` | Message posted from the page via the IPC bridge |

---

## Threading model

All UI operations must run on the main thread. `libhost` enforces this by
routing every IPC callback through `post_to_main_thread()` before the message
reaches `CommandDispatcher::dispatch()`.

`post_to_main_thread()` is implemented per-platform:

- **macOS** — `dispatch_async(dispatch_get_main_queue(), …)`
- **Windows** — `PostMessage` to a hidden `HWND_MESSAGE` window
- **Linux** — `g_idle_add`

The IPC I/O thread never touches UI state directly.

---

## Registry ownership

`WindowRegistry` is the single source of truth for all live objects.

- Each `ManagedWindow` owns at most one *window-backed* `webview::WebView` and
  any number of *view-backed* `ManagedView` overlays.
- Destruction order within `ManagedWindow` is intentional: webviews are
  destroyed before views, and views before the window itself.
- `webview_to_window_` is a flat reverse-lookup map from any webview/view ID to
  its owning window ID, used to resolve all `get_webview()` / `get_view()`
  calls in O(1).

---

## Sentinel window

`Host::run()` creates a hidden 1×1 borderless window before entering the native
event loop. This *sentinel* keeps the loop alive while the controller is still
setting up its first application window, and is never shown to the user.