# libwebview API Reference

> **Scope notice:** libwebview is strictly focused on the webview surface, its navigation, its lifecycle, and its IPC. Window management, geometry, positioning, title, visibility, and state are intentionally out of scope and belong to the native windowing library of your choice. libwebview accepts a `NativeHandle` from that library and parents itself inside it.

---

## webview::WebView

### Constructor

| Signature | Description |
|---|---|
| `WebView(NativeHandle handle, WebViewConfig config = {})` | Creates a new WebView parented to the given native window or view handle |

### webview::WebViewConfig

| Field | Type | Default | Description |
|---|---|---|---|
| `devtools` | `bool` | `false` | Enable the devtools inspector |
| `webview2_user_data_path` | `std::string` | `""` | Custom user data directory for the WebView2 backend (Windows only) |
| `webview2_runtime_path` | `std::string` | `""` | Path to a fixed-version WebView2 runtime (Windows only) |

---

## webview::NativeHandle

A thin wrapper around the platform native window or view pointer passed in from the host windowing library.

### Constructor

| Signature | Description |
|---|---|
| `NativeHandle(void* handle, NativeHandleType type)` | Construct from a raw pointer with an explicit type tag |

### Methods

| Method | Returns | Description |
|---|---|---|
| `get()` | `void*` | Returns the raw underlying pointer |
| `type()` | `NativeHandleType` | Returns the declared handle type |
| `is_window()` | `bool` | True for `NSWindow`, `HWND`, and `GtkWindow` handle types |
| `is_view()` | `bool` | True for `NSView` and `GtkWidget` handle types |

---

## webview::NativeHandleType

| Value | Platform | Description |
|---|---|---|
| `NativeHandleType::NSWindow` | macOS | Top-level Cocoa window |
| `NativeHandleType::NSView` | macOS | Embeddable Cocoa view |
| `NativeHandleType::HWND` | Windows | Top-level or child Win32 window |
| `NativeHandleType::GtkWindow` | Linux | Top-level GTK window |
| `NativeHandleType::GtkWidget` | Linux | Embeddable GTK widget |

---

## Platform Backend Resolution

Each backend resolves the `NativeHandle` type at construction time.

| Backend | `NSWindow*` | `NSView*` | `HWND` | `GtkWindow*` | `GtkWidget*` |
|---|---|---|---|---|---|
| `webview::wkwebview` | Extracts `contentView`, parents `WKWebView` as subview | Parents `WKWebView` directly as subview | — | — | — |
| `webview::webview2` | — | — | Creates `WebView2` with `HWND` as parent, applies `WS_CHILD` if view | — | — |
| `webview::webkit` | — | — | — | Extracts root widget, parents `WebKitWebView` inside | Parents `WebKitWebView` directly inside widget |

---

## WebView — Navigation

| Method | Returns | Description |
|---|---|---|
| `load_url(std::string url)` | `void` | Navigate to a URL |
| `load_html(std::string html)` | `void` | Load a raw HTML string directly |
| `load_file(std::string path)` | `void` | Load a local file by absolute path; resolves to a `file://` URI per platform |
| `reload()` | `void` | Reload the current page |
| `go_back()` | `void` | Navigate back in history |
| `go_forward()` | `void` | Navigate forward in history |
| `get_url()` | `std::string` | Returns the current URL |

---

## WebView — Lifecycle Events

| Method | Callback Signature | Description |
|---|---|---|
| `on_ready(fn)` | `() -> void` | Fired when the webview and DOM are ready |
| `on_close(fn)` | `() -> bool` | Fired when the webview is about to close; return `false` to cancel |
| `on_navigate(fn)` | `(NavigationEvent&) -> void` | Fired before a navigation occurs |
| `on_title_change(fn)` | `(std::string) -> void` | Fired when the page `<title>` changes |

---

## webview::NavigationEvent

| Member | Type/Signature | Description |
|---|---|---|
| `url` | `std::string` | The URL being navigated to |
| `cancel()` | `void` | Cancels the navigation |
| `is_cancelled()` | `bool` | Returns whether the navigation has been cancelled |

---

## WebView — Page Load Events

More granular than `on_ready`, which fires only once on initial load. All callbacks receive a `LoadEvent&` except `on_load_failed`, which receives a `LoadFailedEvent&`.

| Method | Callback Signature | Description |
|---|---|---|
| `on_load_start(fn)` | `(LoadEvent&) -> void` | Fired when a navigation begins committing |
| `on_load_commit(fn)` | `(LoadEvent&) -> void` | Fired when the DOM is parsing but scripts have not yet run |
| `on_load_finish(fn)` | `(LoadEvent&) -> void` | Fired when the page has fully loaded |
| `on_load_failed(fn)` | `(LoadFailedEvent&) -> void` | Fired when a page load fails |

### webview::LoadEvent

| Member | Type | Description |
|---|---|---|
| `url` | `std::string` | The URL that loaded |
| `is_main_frame` | `bool` | `false` for subframes and iframes; defaults to `true` |

### webview::LoadFailedEvent : LoadEvent

Inherits all members of `LoadEvent`.

| Member | Type | Description |
|---|---|---|
| `error_code` | `int` | Platform error code |
| `error_description` | `std::string` | Human-readable reason |

---

## WebView — Permissions

If no handler is registered, permission requests are denied by default.

| Method | Callback Signature | Description |
|---|---|---|
| `on_permission_request(fn)` | `(PermissionRequest&) -> void` | Fired when the page requests a sensitive capability |

### webview::PermissionRequest

| Member | Type/Signature | Description |
|---|---|---|
| `permission` | `PermissionType` | The permission being requested |
| `origin` | `std::string` | The origin making the request |
| `grant()` | `void` | Grants the permission |
| `deny()` | `void` | Denies the permission |
| `is_decided()` | `bool` | True if `grant()` or `deny()` has been called |
| `is_granted()` | `bool` | True if `grant()` was called |

### webview::PermissionType

| Value | Description |
|---|---|
| `Camera` | `getUserMedia` video |
| `Microphone` | `getUserMedia` audio |
| `Geolocation` | `navigator.geolocation` |
| `Notifications` | `Notification.requestPermission` |
| `ClipboardRead` | `navigator.clipboard.readText` |
| `Midi` | Web MIDI API |

---

## WebView — Downloads

Three-phase callback model sharing a single `DownloadEvent` handle across the full lifecycle of one download. The `id` field is stable across all three callbacks and can be used to correlate them.

| Method | Callback Signature | Description |
|---|---|---|
| `on_download_start(fn)` | `(DownloadEvent&) -> bool` | Fired when a download begins; return `false` to cancel |
| `on_download_progress(fn)` | `(DownloadEvent&) -> void` | Fired periodically with updated byte counts |
| `on_download_complete(fn)` | `(DownloadEvent&) -> void` | Fired when the download finishes or fails; check `is_failed()` |

### webview::DownloadEvent

| Member | Type/Signature | Description |
|---|---|---|
| `id` | `std::string` | Unique ID for this download, stable across all three callbacks |
| `url` | `std::string` | Source URL |
| `suggested_filename` | `std::string` | Filename suggested by the server |
| `destination` | `std::string` | Writable in `on_download_start` to override the save path |
| `bytes_received` | `int64_t` | Bytes received so far |
| `total_bytes` | `int64_t` | Total size; `-1` if unknown |
| `is_failed()` | `bool` | True if the download ended in an error |
| `cancel()` | `void` | Cancels an in-progress download |

---

## WebView — New Window Intercept

Without a registered handler, the default platform behaviour applies (popups are typically blocked silently).

| Method | Callback Signature | Description |
|---|---|---|
| `on_new_window(fn)` | `(NewWindowEvent&) -> void` | Fired when the page attempts to open a new window or tab |

### webview::NewWindowEvent

| Member | Type/Signature | Description |
|---|---|---|
| `url` | `std::string` | Target URL |
| `is_user_gesture` | `bool` | Whether the open was triggered by a user gesture |
| `cancel()` | `void` | Suppresses the new window |
| `redirect(std::string url)` | `void` | Navigates the current webview to this URL instead |
| `is_cancelled()` | `bool` | True if `cancel()` has been called |
| `redirect_url()` | `const std::string&` | Returns the URL set by `redirect()`, or empty if not called |

---

## WebView — Authentication Challenges

| Method | Callback Signature | Description |
|---|---|---|
| `on_auth_challenge(fn)` | `(AuthChallenge&) -> void` | Fired when the server or proxy requires HTTP authentication |

### webview::AuthChallenge

| Member | Type/Signature | Description |
|---|---|---|
| `host` | `std::string` | Server host |
| `realm` | `std::string` | HTTP realm string |
| `is_proxy` | `bool` | True if this is a proxy auth challenge |
| `respond(std::string user, std::string password)` | `void` | Provides credentials |
| `cancel()` | `void` | Cancels the request |

---

## WebView — Console Messages

Surfaces renderer-side log output to the host without requiring devtools to be open.

> **Platform note:** WKWebView does not expose a native console message API. On macOS, `source_url` and `line` are best-effort and may be empty — console output is captured via an injected JS shim rather than the engine's own reporting channel.

| Method | Callback Signature | Description |
|---|---|---|
| `on_console_message(fn)` | `(ConsoleMessage&) -> void` | Fired whenever the page writes to the browser console |

### webview::ConsoleMessage

| Member | Type | Description |
|---|---|---|
| `level` | `ConsoleLevel` | `Log`, `Info`, `Warn`, or `Error` |
| `text` | `std::string` | The message text |
| `source_url` | `std::string` | Script URL, if available; best-effort on WKWebView |
| `line` | `int` | Source line number |

### webview::ConsoleLevel

| Value |
|---|
| `ConsoleLevel::Log` |
| `ConsoleLevel::Info` |
| `ConsoleLevel::Warn` |
| `ConsoleLevel::Error` |

---

## WebView — Script Execution

`add_user_script` injects JS into every page at load time. This is the same mechanism used internally to install the IPC bridge shim.

| Method | Returns | Description |
|---|---|---|
| `eval(std::string js, fn)` | `void` | Evaluate a JS expression in the current page; `fn` receives an `EvalResult` |
| `add_user_script(std::string js, ScriptInjectTime time)` | `void` | Inject a script into every page at the specified point in the load cycle |
| `remove_user_scripts()` | `void` | Remove all previously registered user scripts |

### webview::ScriptInjectTime

| Value | Description |
|---|---|
| `DocumentStart` | Injected before any page scripts run |
| `DocumentEnd` | Injected after the DOM is ready |

### webview::EvalResult

| Member | Type | Description |
|---|---|---|
| `value` | `json` | The serialised return value of the expression |
| `error` | `std::optional<std::string>` | Set if the expression threw |
| `ok()` | `bool` | True if `error` has no value |

---

## WebView — Find in Page

| Method | Returns | Description |
|---|---|---|
| `find(std::string query, FindOptions opts, fn)` | `void` | Begin a find session; `fn` receives a `FindResult` on each match update |
| `find_next()` | `void` | Advance to the next match |
| `find_prev()` | `void` | Go back to the previous match |
| `stop_find()` | `void` | End the find session and clear highlights |

### webview::FindOptions

| Field | Type | Default | Description |
|---|---|---|---|
| `case_sensitive` | `bool` | `false` | Match case exactly |
| `wrap` | `bool` | `true` | Wrap around when reaching the end of the page |

### webview::FindResult

| Member | Type | Description |
|---|---|---|
| `match_count` | `int` | Total number of matches found |
| `active_match` | `int` | 0-based index of the currently highlighted match |

---

## WebView — Zoom

Zoom is applied at the webview compositor layer, equivalent to a CSS `zoom` on the root. It is distinct from browser-level font scaling.

| Method | Returns | Description |
|---|---|---|
| `set_zoom(double factor)` | `void` | Set the zoom level; `1.0` = 100%, `1.5` = 150%, etc. |
| `get_zoom()` | `double` | Returns the current zoom factor |

---

## WebView — User Agent

Overrides the `User-Agent` header sent with all requests from this webview instance. Must be called before the first navigation to guarantee the override is in effect for the initial load.

| Method | Returns | Description |
|---|---|---|
| `set_user_agent(std::string ua)` | `void` | Override the User-Agent string for this webview instance |
| `get_user_agent()` | `std::string` | Returns the currently active User-Agent string |

---

## WebView — Cache

`clear_cache` targets the HTTP response cache only. Cookies and persistent storage are not affected; use the Cookie and Storage APIs for those.

| Method | Returns | Description |
|---|---|---|
| `clear_cache()` | `void` | Clears the HTTP response cache for this webview instance |

---

## WebView — Cookies

Minimal cross-platform cookie management scoped to this webview instance. Only the fields listed on `Cookie` are read and written on all three backends — additional attributes such as `SameSite` and `Priority` are not surfaced because support is inconsistent across engines.

| Method | Returns | Description |
|---|---|---|
| `get_cookies(std::string url, fn)` | `void` | Fetch all cookies matching the given URL; `fn` receives `std::vector<Cookie>` |
| `set_cookie(Cookie cookie, fn)` | `void` | Set a cookie; `fn` receives a `bool` indicating success |
| `delete_cookie(std::string name, std::string url, fn)` | `void` | Delete a named cookie for a URL; `fn` receives a `bool` indicating success |
| `clear_cookies()` | `void` | Delete all cookies for this webview instance |

### webview::Cookie

| Field | Type | Default | Description |
|---|---|---|---|
| `name` | `std::string` | — | Cookie name |
| `value` | `std::string` | — | Cookie value |
| `domain` | `std::string` | — | Cookie domain |
| `path` | `std::string` | `"/"` | Cookie path |
| `expires` | `std::optional<int64_t>` | `nullopt` | Unix timestamp expiry; `nullopt` for session cookies |
| `secure` | `bool` | `false` | Restrict to HTTPS |
| `http_only` | `bool` | `false` | Inaccessible to JS via `document.cookie` |

---

## WebView — Request Interception

> **Feature flag:** `WEBVIEW_FEATURE_REQUEST_INTERCEPT`
>
> Defined on macOS and Windows. Not defined on Linux. Check this flag before registering a handler and design for possible degraded behaviour where it is absent.

| Method | Callback Signature | Description |
|---|---|---|
| `on_request(fn)` | `(ResourceRequest&) -> void` | Fired before each resource request is sent |

### webview::ResourceRequest

| Member | Type/Signature | Description |
|---|---|---|
| `url` | `std::string` | Full request URL |
| `method` | `std::string` | HTTP method: `GET`, `POST`, etc. |
| `headers` | `std::map<std::string, std::string>` | Request headers |
| `resource_type` | `ResourceType` | Categorisation of the requested resource |
| `cancel()` | `void` | Blocks the request entirely |
| `redirect(std::string url)` | `void` | Redirects the request to another URL |
| `respond(ResourceResponse)` | `void` | Serves a synthetic response, bypassing the network |

### webview::ResourceType

| Value |
|---|
| `Document` |
| `Script` |
| `Image` |
| `Fetch` |
| `Other` |

### webview::ResourceResponse

| Field | Type | Default | Description |
|---|---|---|---|
| `status` | `int` | `200` | HTTP status code |
| `headers` | `std::map<std::string, std::string>` | `{}` | Response headers |
| `body` | `std::vector<uint8_t>` | `{}` | Response body bytes |

---

## WebView — File Dialog

| Method | Returns | Description |
|---|---|---|
| `dialog(FileDialog d)` | `std::optional<std::string>` | Show a native file dialog; returns the selected path or `nullopt` if cancelled |
| `dialog_multi(FileDialog d)` | `std::vector<std::string>` | Show a native multi-select dialog; returns list of selected paths |

### webview::FileDialog

| Field | Type | Default | Description |
|---|---|---|---|
| `mode` | `FileDialog::Mode` | `Open` | `Open`, `Save`, or `OpenMultiple` |
| `filters` | `std::vector<FileFilter>` | `{}` | List of file type filters |
| `default_path` | `std::string` | `""` | Initial directory or file path |
| `title` | `std::string` | `""` | Dialog window title |

### webview::FileFilter

| Field | Type | Description |
|---|---|---|
| `name` | `std::string` | Display name for this filter group |
| `extensions` | `std::vector<std::string>` | File extensions without leading dot, e.g. `{"png", "jpg"}` |

---

## WebView — IPC

Accessed via `wv.ipc`.

> **IPC Architecture & Limitations:**
> IPC is **only** supported when loading content via `load_file` or `load_html`. These methods abstract the content using a custom `webview://` scheme to facilitate the communication bridge. IPC is **not** supported for external URLs loaded via `load_url`.
>
> **How it works:** The IPC bridge operates over the `webview://` scheme, driven by a C++ queue system. It works through a mix of JavaScript injection and HTTP GET scheme handlers to process requests and fully support `arraybuffer` (binary) IPC.

### JSON — Two-way

| Method | Description |
|---|---|
| `ipc.handle(std::string channel, fn)` | Register a handler on the host for a named channel; `fn` receives a `Message&`. Call `msg.reply()` or `msg.reject()` to resolve the renderer-side Promise |
| `ipc.invoke(std::string channel, json body, fn)` | Invoke a named channel on the renderer; `fn` receives the renderer's reply as a `Message&` |

### JSON — Fire and Forget

| Method | Description |
|---|---|
| `ipc.send(std::string channel, json body)` | Send a JSON message from host to renderer with no reply |
| `ipc.on(std::string channel, fn)` | Listen for a JSON message from the renderer; `fn` receives a `Message&`; no reply expected |

### Binary — Two-way

| Method | Description |
|---|---|
| `ipc.handle(std::string channel, fn)` | Register a binary handler on the host; `fn` receives a `BinaryMessage&`. Call `msg.reply()` with `std::vector<uint8_t>` to resolve the renderer-side Promise |
| `ipc.invoke(std::string channel, std::vector<uint8_t> data, fn)` | Invoke a binary channel on the renderer; `fn` receives the renderer's binary reply as a `BinaryMessage&` |

### Binary — Fire and Forget

| Method | Description |
|---|---|
| `ipc.send_binary(std::string channel, std::vector<uint8_t> data)` | Send raw bytes from host to renderer with no reply |
| `ipc.on_binary(std::string channel, fn)` | Listen for raw bytes from the renderer; `fn` receives a `BinaryMessage&`; no reply expected |

---

## webview::Message

| Member | Type/Signature | Description |
|---|---|---|
| `body` | `json` | The JSON payload |
| `channel` | `std::string` | The channel this message was received on |
| `reply(json body)` | `void` | Resolve the renderer-side Promise with a JSON payload |
| `reject(std::string reason)` | `void` | Reject the renderer-side Promise with a reason string |

---

## webview::BinaryMessage

| Member | Type/Signature | Description |
|---|---|---|
| `data` | `std::vector<uint8_t>` | The raw binary payload |
| `channel` | `std::string` | The channel this message was received on |
| `reply(std::vector<uint8_t> data)` | `void` | Resolve the renderer-side Promise with raw bytes |
| `reject(std::string reason)` | `void` | Reject the renderer-side Promise with a reason string |

---

## Platform Internals

| Macro / Symbol | Description |
|---|---|
| `WEBVIEW_PLATFORM` | String identifying the current platform: `"linux"`, `"windows"`, or `"macos"` |
| `WEBVIEW_FEATURE_REQUEST_INTERCEPT` | Defined on macOS and Windows when full request interception is available; not defined on Linux |
| `webview::webkit` | Internal Linux backend (GTK + WebKitGTK) |
| `webview::webview2` | Internal Windows backend (WebView2) |
| `webview::wkwebview` | Internal macOS backend (WKWebView) |