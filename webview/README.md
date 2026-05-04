# libwebview

A lightweight, modern C++20 library that embeds a native webview into your application. It wraps **WKWebView** on macOS, **WebView2** on Windows, and **WebKitGTK** on Linux behind a single, consistent API.

libwebview is strictly scoped to the webview surface: navigation, lifecycle, IPC, cookies, scripting, and request interception. Window management, geometry, and positioning are intentionally out of scope — you bring your own windowing library and hand libwebview a `NativeHandle`.

---

## Platform Status

| Platform | Backend    | Status         |
|----------|------------|----------------|
| macOS    | WKWebView  | ✅ Implemented |
| Windows  | WebView2   | ✅ Implemented |
| Linux    | WebKitGTK  | ✅ Implemented |

---

## Requirements

- C++20
- CMake 3.21+
- [nlohmann/json](https://github.com/nlohmann/json) (consumed via `find_package`)

**macOS:** Xcode / Apple Clang, macOS 11+ recommended (macOS 10.15+ supported with degraded feature availability)

**Windows:** MSVC (Visual Studio 2022 recommended) and the [WebView2 SDK](https://github.com/microsoft/wil) (consumed via `find_package(unofficial-webview2)`)

**Linux:** GCC / Clang, `pkg-config`, `gtk+-3.0`, and `webkit2gtk-4.1` (WebKitGTK 4.1+ required for POST body IPC support)

---

## Building

libwebview uses CMake and expects its dependencies to be available via `find_package`. [vcpkg](https://vcpkg.io) or [Conan](https://conan.io) both work.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build
```

The install step places the static library in `lib/` and headers under `include/webview/`.

### Linking in Your Project

```cmake
find_package(libwebview CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE webview)
```

---

## Quick Start

```cpp
#include <webview/webview.hpp>

// Assumes you already have a native window from your windowing library.
// On macOS, pass your NSWindow* as the handle.
webview::NativeHandle handle(myNSWindow, webview::NativeHandleType::NSWindow);

webview::WebViewConfig config;
config.devtools = true;

#if defined(_WIN32)
config.webview2_user_data_path = "C:\\MyApp\\Cache";
config.webview2_runtime_path   = ""; // Use system Evergreen runtime
#endif

webview::WebView wv(handle, config);

wv.on_ready([&] {
    wv.ipc.send("greet", { "hello", "from host" });
});

wv.load_html(R"html(
    <!DOCTYPE html>
    <html>
    <body>
        <script>
            window.ipc.on('greet', (msg) => {
                document.body.innerText = JSON.stringify(msg);
            });
        </script>
    </body>
    </html>
)html");
```

---

## NativeHandle

`NativeHandle` is a thin, typed wrapper around the platform window or view pointer you provide from your windowing library.

```cpp
NativeHandle(myNSWindow,   NativeHandleType::NSWindow);  // macOS — top-level window
NativeHandle(myNSView,     NativeHandleType::NSView);    // macOS — embeddable view
NativeHandle(myHWND,       NativeHandleType::HWND);      // Windows
NativeHandle(myGtkWindow,  NativeHandleType::GtkWindow); // Linux
NativeHandle(myGtkWidget,  NativeHandleType::GtkWidget); // Linux
```

libwebview parents itself as a subview or child widget filling the handle's bounds. It does not create or manage the window.

---

## API Reference

### Navigation

```cpp
wv.load_url("https://example.com");
wv.load_html("<h1>Hello</h1>");
wv.load_file("/path/to/index.html");
wv.reload();
wv.go_back();
wv.go_forward();

std::string url = wv.get_url();
```

> **Note:** `ipc.send`, `ipc.handle`, and related methods only work when content is loaded via `load_html` or `load_file`. IPC is not supported for external URLs loaded via `load_url`.

---

### Lifecycle Events

```cpp
wv.on_ready([&] {
    // DOM is ready; safe to inject scripts or send IPC messages.
});

wv.on_close([&]() -> bool {
    return true; // Return false to prevent closing.
});

wv.on_navigate([&](webview::NavigationEvent& ev) {
    if (ev.url.find("blocked.example.com") != std::string::npos)
        ev.cancel();
});

wv.on_title_change([&](std::string title) {
    // Update your window title here.
});
```

---

### Page Load Events

```cpp
wv.on_load_start([](webview::LoadEvent& ev)  { /* navigation began             */ });
wv.on_load_commit([](webview::LoadEvent& ev) { /* DOM parsing, scripts not run */ });
wv.on_load_finish([](webview::LoadEvent& ev) { /* fully loaded                 */ });

wv.on_load_failed([](webview::LoadFailedEvent& ev) {
    // ev.error_code, ev.error_description
});
```

---

### Script Execution

```cpp
// Evaluate a one-off expression
wv.eval("document.title", [](webview::EvalResult r) {
    if (r.ok()) std::cout << r.value.dump() << "\n";
});

// Inject a persistent script into every page at document start
wv.add_user_script("window.MY_FLAG = true;", webview::ScriptInjectTime::DocumentStart);

// Remove all injected scripts
wv.remove_user_scripts();
```

---

### IPC

IPC is bidirectional and supports both JSON and raw binary payloads. It requires content loaded via `load_html` or `load_file`. The JS bridge is available at `window.ipc`.

#### Host → Renderer (fire and forget)

```cpp
wv.ipc.send("news", { {"headline", "Hello from host"} });
```
```js
window.ipc.on('news', (msg) => console.log(msg.headline));
```

#### Renderer → Host (fire and forget)

```js
window.ipc.send('log', { level: 'info', text: 'hi' });
```
```cpp
wv.ipc.on("log", [](webview::Message& msg) {
    std::cout << msg.body["text"] << "\n";
});
```

#### Host → Renderer (invoke, awaits reply)

```cpp
wv.ipc.invoke("compute", { {"x", 6} }, [](webview::Message& reply) {
    std::cout << "result: " << reply.body["result"] << "\n";
});
```
```js
window.ipc.handle('compute', async (body) => {
    return { result: body.x * 7 };
});
```

#### Renderer → Host (invoke, awaits reply)

```js
const result = await window.ipc.invoke('add', { a: 1, b: 2 });
console.log(result); // 3
```
```cpp
wv.ipc.handle("add", [](webview::Message& msg) {
    int a = msg.body["a"], b = msg.body["b"];
    msg.reply({ {"result", a + b} });
    // or: msg.reject("something went wrong");
});
```

#### Binary IPC

```cpp
// Host sends raw bytes
std::vector<uint8_t> data = { 0xDE, 0xAD, 0xBE, 0xEF };
wv.ipc.send_binary("frame", data);

// Host handles an incoming binary invoke
wv.ipc.handle("upload", [](webview::BinaryMessage& msg) {
    // msg.data is std::vector<uint8_t>
    msg.reply({ 0x01 });
});
```
```js
// JS sends binary
const buf = new Uint8Array([1, 2, 3]).buffer;
window.ipc.sendBinary('upload', buf);

// JS listens for binary
window.ipc.onBinary('frame', (buf) => {
    const view = new Uint8Array(buf);
    console.log(view);
});
```

---

### Cookies

```cpp
// Get all cookies for a URL
wv.get_cookies("https://example.com", [](std::vector<webview::Cookie> cookies) {
    for (auto& c : cookies)
        std::cout << c.name << "=" << c.value << "\n";
});

// Set a cookie
webview::Cookie cookie;
cookie.name   = "session";
cookie.value  = "abc123";
cookie.domain = "example.com";
cookie.secure = true;
wv.set_cookie(cookie, [](bool ok) { /* ... */ });

// Delete a specific cookie
wv.delete_cookie("session", "https://example.com", [](bool found) { /* ... */ });

// Clear all cookies
wv.clear_cookies();
```

---

### Request Interception

> Requires the `WEBVIEW_FEATURE_REQUEST_INTERCEPT` feature flag (defined automatically on macOS and Windows; omitted on Linux WebKitGTK due to synchronous blocking limitations). Register your handler before the first navigation.

```cpp
wv.on_request([](webview::ResourceRequest& req) {
    if (req.url.find("/api/secret") != std::string::npos) {
        req.cancel();
        return;
    }

    if (req.url.find("/mock") != std::string::npos) {
        webview::ResourceResponse res;
        res.status                    = 200;
        res.headers["Content-Type"]   = "application/json";
        std::string body              = R"({"mocked":true})";
        res.body                      = std::vector<uint8_t>(body.begin(), body.end());
        req.respond(std::move(res));
        return;
    }

    // req.redirect("https://other.example.com/path");
});
```

---

### File Dialogs

```cpp
webview::FileDialog d;
d.title   = "Open Image";
d.filters = { { "Images", { "png", "jpg", "gif" } } };

// Single file open
d.mode = webview::FileDialog::Mode::Open;
auto path = wv.dialog(d);
if (path) std::cout << "Selected: " << *path << "\n";

// Multi-select
auto paths = wv.dialog_multi(d);

// Save dialog
d.mode         = webview::FileDialog::Mode::Save;
d.default_path = "/home/user/documents";
auto dest      = wv.dialog(d);
```

---

### Permissions

```cpp
wv.on_permission_request([](webview::PermissionRequest& req) {
    if (req.permission == webview::PermissionType::Camera)
        req.grant();
    else
        req.deny();
});
```

---

### Downloads

```cpp
wv.on_download_start([](webview::DownloadEvent& ev) -> bool {
    ev.destination = "/tmp/" + ev.suggested_filename;
    return true; // Return false to cancel.
});

wv.on_download_progress([](webview::DownloadEvent& ev) {
    std::cout << ev.bytes_received << " / " << ev.total_bytes << "\n";
});

wv.on_download_complete([](webview::DownloadEvent& ev) {
    if (ev.is_failed()) std::cerr << "Download failed\n";
    else                std::cout << "Done: " << ev.destination << "\n";
});
```

---

### Zoom & User Agent

```cpp
wv.set_zoom(1.5);
double z = wv.get_zoom();

wv.set_user_agent("MyApp/1.0");
std::string ua = wv.get_user_agent();
```

---

### Cache

```cpp
// Clears the HTTP response cache only. Cookies and storage are unaffected.
wv.clear_cache();
```

---

### Console Messages

```cpp
wv.on_console_message([](webview::ConsoleMessage& msg) {
    std::cout << "[JS " << (int)msg.level << "] " << msg.text << "\n";
});
```

> **Note (macOS):** `source_url` and `line` are best-effort. Console output is captured via an injected JS shim rather than the engine's native reporting channel.

---

### Authentication Challenges

```cpp
wv.on_auth_challenge([](webview::AuthChallenge& ac) {
    if (ac.host == "internal.example.com")
        ac.respond("admin", "hunter2");
    else
        ac.cancel();
});
```

---

### New Window Intercept

```cpp
wv.on_new_window([&](webview::NewWindowEvent& ev) {
    // Redirect popups into the current view instead of opening a new window.
    ev.redirect(ev.url);
});
```

---

## Windows: Downloading the WebView2 Runtime

If you are not relying on the system Evergreen runtime, use the included Go script to download and unpack a fixed-version WebView2 runtime:

```bash
go run scripts/main.go -version "146.0.3856.97" -dest "C:\MyApp\WebView2Runtime"
```

---

## Feature Flags

| Macro                             | Description                                        |
|-----------------------------------|----------------------------------------------------|
| `WEBVIEW_PLATFORM`                | `"macos"`, `"windows"`, or `"linux"`               |
| `WEBVIEW_FEATURE_REQUEST_INTERCEPT` | Defined when the backend supports request interception |

---

## License

MIT