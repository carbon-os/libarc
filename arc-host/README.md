# arc-host

The native host process for libarc. It owns the platform event loop, the IPC
server, and all managed windows and webviews. A controller (typically a Go
process) connects over IPC and drives it via JSON commands.

---

## Building

### Prerequisites

- CMake 3.21+
- A C++20 compiler (Clang 15+ on macOS, MSVC 2022+ on Windows, GCC 12+ on Linux)
- vcpkg with `nlohmann-json` installed
- macOS 13.0+ (for the WKWebView inspector to work correctly)

### Configure and build

```bash
cmake -B build -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build
```

The macOS build automatically signs the binary with an ad-hoc identity (`-`)
after linking. No Apple Developer account is required for local development.

---

## macOS Code Signing

Entitlements are embedded automatically at build time via a `POST_BUILD`
`codesign` step in `arc-host/CMakeLists.txt`. The hardened runtime
(`--options runtime`) is always enabled so the entitlement keys take effect.

### Identities

| Scenario | `ARC_SIGN_IDENTITY` value |
|---|---|
| Local dev / CI (no account) | `-` *(ad-hoc, default)* |
| Apple Developer account | `Apple Development: Name (TEAMID)` |
| Distribution / notarization | `Apple Distribution: Name (TEAMID)` |

Override the identity at configure time:

```bash
cmake -B build \
  -DARC_SIGN_IDENTITY="Apple Development: Your Name (TEAMID)" \
  -DCMAKE_TOOLCHAIN_FILE=...
cmake --build build
```

### Verify embedded entitlements

```bash
codesign -d --entitlements :- build/arc-host/arc-host
```

### Active entitlements

| Key | Purpose |
|---|---|
| `com.apple.security.cs.allow-jit` | WKWebView JIT — required for JS execution in both the page and the DevTools inspector |
| `com.apple.security.cs.disable-library-validation` | Allows loading third-party dylibs not signed with your team ID |
| `com.apple.security.network.client` | Outbound network — required for WebKit's internal devtools protocol server |
| `com.apple.security.get-task-allow` | Allows debuggers and the WebKit remote inspector to attach |

> **App Store note:** `get-task-allow` must be removed before submission.
> All other keys above are App Store safe.

---

## Running

```
arc-host --ipc-channel <id> [--mode <managed|embedded>] [--module <path>]
```

| Flag | Description |
|---|---|
| `--ipc-channel <id>` | Channel ID for the IPC transport. Required in managed mode; optional in embedded mode (auto-generated if omitted). |
| `--mode <managed\|embedded>` | `managed` *(default)*: a controller process connects over IPC. `embedded`: arc-host loads the module itself and generates the channel ID. |
| `--module <path>` | Path to a `.dylib` / `.so` / `.dll` that exports `void AppMain(const char* channel_id)`. Embedded mode only. |

### Managed mode (typical)

The controller launches arc-host and passes a pre-negotiated channel ID:

```bash
./arc-host --ipc-channel arc-12345
```

### Embedded mode

arc-host loads the module in-process on a detached thread. The module connects
back as an `ipc::Client` using the provided channel ID:

```bash
./arc-host --mode embedded --module ./libmyapp.dylib
# channel ID is auto-generated; passed to AppMain as a C string
```