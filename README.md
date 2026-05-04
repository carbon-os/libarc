# libarc

libarc is the native host engine for the ARC Desktop Framework. It owns the
platform event loop, all native windows and webviews, and the IPC server.

A controller — typically a Go process, or a language-specific ARC SDK — connects
over IPC and drives libarc entirely through JSON commands. libarc has no
opinions about application logic; it is a controlled surface, not a framework
with an opinion about your code.

---

## Architecture

```
┌──────────────────────────────────────────────────────┐
│                  Controller Process                  │
│          (Go, ARC SDK, or any IPC client)            │
└────────────────────────┬─────────────────────────────┘
                         │ JSON commands over IPC
                         │ (Unix socket / Named Pipe)
┌────────────────────────▼─────────────────────────────┐
│                      arc-host                        │
│                                                      │
│  ┌───────────────┐  ┌───────────┐  ┌──────────────┐  │
│  │  IPC          │  │ Windowing │  │   Webview    │  │
│  │               │  │           │  │              │  │
│  │ Transport,    │  │ Windows,  │  │ WKWebView    │  │
│  │ framing, and  │  │ views,    │  │ WebView2     │  │
│  │ delivery      │  │ PixelView │  │ WebKitGTK    │  │
│  └───────────────┘  └───────────┘  └──────────────┘  │
└──────────────────────────────────────────────────────┘
```

**arc-host** is the compiled binary. It starts, owns the platform event loop,
and waits for a controller to connect.

The **host** wires IPC, windowing, and the webview together and implements the
JSON command protocol the controller speaks.

The **IPC layer** handles connection lifecycle, message framing, and delivery
over Unix domain sockets (macOS / Linux) or named pipes (Windows). It accepts
exactly one controller per host instance.

The **windowing layer** manages native windows and views — creation, geometry,
state, and appearance — and exposes the native handles the webview parents into.

The **webview layer** embeds a native webview into a windowing surface. It
handles navigation, scripting, bidirectional IPC with the page, cookies, request
interception, and downloads.

---

## Repository Layout

```
libarc/
├── arc-host/     # arc-host executable and entrypoint
├── host/         # Host — command protocol and event loop wiring
├── ipc/          # IPC — transport, framing, and delivery
├── ui/           # Windowing — windows, views, and pixel surfaces
├── webview/      # Webview — native webview embedding
├── samples/      # Example controllers and usage
├── scripts/      # Build utilities (e.g. WebView2 runtime downloader)
├── vcpkg.json    # Dependency manifest
└── CMakeLists.txt
```

Each subdirectory has its own README with its full API surface.

---

## Platform Support

| Platform | Window Backend | Webview Backend  | IPC Transport      |
|----------|----------------|------------------|--------------------|
| macOS    | Cocoa/AppKit   | WKWebView        | Unix domain socket |
| Windows  | Win32          | WebView2         | Named pipe         |
| Linux    | GTK 3          | WebKitGTK 4.1    | Unix domain socket |

Minimum versions: macOS 13.0, Windows 10 (with WebView2 runtime), Ubuntu 22.04+.

---

## Building

### Prerequisites

| Platform    | Requirements                                                      |
|-------------|-------------------------------------------------------------------|
| **All**     | CMake ≥ 3.22, C++20 compiler, Git                                 |
| **macOS**   | Xcode Command Line Tools                                          |
| **Windows** | Visual Studio 2022 (MSVC)                                         |
| **Linux**   | GCC/Clang, `pkg-config`, `libwebkit2gtk-4.1-dev`, `libgtk-3-dev` |

### 1 — Clone vcpkg

```bash
git clone https://github.com/microsoft/vcpkg.git
```

### 2 — Bootstrap vcpkg

```bash
# macOS / Linux
./vcpkg/bootstrap-vcpkg.sh

# Windows (PowerShell)
.\vcpkg\bootstrap-vcpkg.bat
```

### 3 — Install dependencies

```bash
./vcpkg/vcpkg install
```

vcpkg reads `vcpkg.json` and installs all listed dependencies (`nlohmann-json`
on all platforms, `webview2` on Windows only).

### 4 — Configure

```bash
# macOS / Linux
cmake -B build \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=./vcpkg/scripts/buildsystems/vcpkg.cmake

# Windows
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
      "-DCMAKE_TOOLCHAIN_FILE=./vcpkg/scripts/buildsystems/vcpkg.cmake"
```

### 5 — Build

```bash
cmake --build build --config Release
```

Output artifacts are placed under `build/bin/` and `build/lib/`.

On macOS, `arc-host` is automatically code-signed with an ad-hoc identity
after linking. No Apple Developer account is required for local development.
See [arc-host/README.md](arc-host/README.md) for signing options.

---

## Running arc-host

```
arc-host --ipc-channel <id> [--mode <managed|embedded>] [--module <path>] [--logging]
```

| Flag | Description |
|------|-------------|
| `--ipc-channel <id>` | Channel ID for the IPC transport. Required in managed mode. |
| `--mode <managed\|embedded>` | `managed` *(default)*: a controller connects over IPC. `embedded`: arc-host loads a module in-process. |
| `--module <path>` | Path to a `.dylib` / `.so` / `.dll` exporting `void AppMain(const char*)`. Embedded mode only. |
| `--logging` | Enable diagnostic logging to stdout. |

### Managed mode (typical)

The controller launches arc-host and connects to the negotiated channel:

```bash
./build/bin/arc-host --ipc-channel arc-12345 --logging
```

### Embedded mode

arc-host loads the module in-process. The module connects back as an IPC client
using the provided channel ID:

```bash
./build/bin/arc-host --mode embedded --module ./libmyapp.dylib
```

---

## IPC Protocol

The controller speaks JSON over the IPC channel. Every message is a framed JSON
object. The host dispatches commands to the appropriate subsystem and emits
events back to the controller.

The wire format uses a fixed 12-byte frame header:

```
magic (u32) | version (u8) | type (u8) | reserved (u16) | payload_len (u32)
```

Full wire format, error types, and the in-process transport (for when the
controller runs as a `.so` in the same process) are documented in
[ipc/README.md](ipc/README.md).

---

## Controllers and SDKs

libarc is intentionally a controlled surface. Everything it can do is exposed
through the IPC command protocol — the controller is where application logic
lives.

The canonical controller is a Go process using the ARC Go SDK, but anything
that can open a socket or named pipe and speak the JSON protocol can drive
arc-host. Language SDKs are thin clients: they connect, send commands, and
receive events. They carry no native code of their own.

---

## Component Documentation

| Component | README |
|-----------|--------|
| arc-host | [arc-host/README.md](arc-host/README.md) |
| Host | [host/README.md](host/README.md) |
| IPC | [ipc/README.md](ipc/README.md) |
| Windowing | [ui/README.md](ui/README.md) |
| Webview | [webview/README.md](webview/README.md) |