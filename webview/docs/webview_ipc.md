# webview_ipc.md

---

## How It Works

Custom URL schemes are the foundation. Every major WebView lets you register a scheme (`webview://`, `myapp://`, etc.) and intercept all requests to it — including POST with a raw binary body. That's all you need.

---

### JS → C++ (Renderer to Host)

JS already speaks HTTP. Just POST your data — string or ArrayBuffer — directly to a scheme URL:

```js
fetch('webview://ipc/renderer-to-host/your-channel', {
    method: 'POST',
    body: myArrayBuffer
});
```

Your native scheme handler receives the request and reads the body as raw bytes. Done.

---

### C++ → JS (Host to Renderer)

This direction has no native push API, so it's one extra step. The host evaluates a script to notify the page of a waiting message, then the page GETs it back through the scheme handler:

```
1. host calls:   evaluateJavaScript("window.ipc._receive('your-token')")
2. page calls:   GET webview://ipc/host-to-renderer/your-token
3. handler:      look up token → return bytes → (optionally) remove from queue
```

The host side is just a map (or queue) keyed by token:

```cpp
// Host stores data
queue["your-token"] = myBytes;

// Host notifies page
eval("window.ipc._receive('your-token')");
```

```js
// Page receives notification and pulls
window.ipc._receive = async (token) => {
    const res = await fetch('webview://ipc/host-to-renderer/' + token);
    const buf = await res.arrayBuffer();
    ipc.emit(buf);
};
```

The queue on the C++ side is only needed if you push multiple messages before the page has consumed the previous one. For simple cases a plain map is fine.

---

### The rest is just wrapper

Everything else — `ipc.send()`, `ipc.on()`, channels, event emitters — is convenience built on top of these two flows. The transport is just fetch POST and fetch GET through a custom scheme.

---

## Platform Support

| | **WKWebView** (macOS/iOS) | **WebKitGTK** (Linux) | **WebView2** (Windows) |
|---|---|---|---|
| **Register scheme** | `setURLSchemeHandler(_:forURLScheme:)` on `WKWebViewConfiguration` | `webkit_web_context_register_uri_scheme()` | `CoreWebView2CustomSchemeRegistration` + `AddWebResourceRequestedFilter()` |
| **Handler entry point** | `WKURLSchemeHandler::webView(_:start:)` | `WebKitURISchemeRequestCallback` | `CoreWebView2::WebResourceRequested` event |
| **GET** | ✅ | ✅ | ✅ |
| **POST** | ✅ | ✅ since 2.36 | ✅ |
| **POST binary body** | ✅ `request.httpBody` (`NSData*`) | ✅ `webkit_uri_scheme_request_get_http_body()` (`GInputStream*`) — WebKit 4.1+ | ✅ `Request.Content` (`IStream*`) |
| **Inject script** | `evaluateJavaScript(_:completionHandler:)` | `webkit_web_view_evaluate_javascript()` | `ExecuteScript()` |
| **Inject at doc start** | `WKUserScript` + `WKUserScriptInjectionTimeAtDocumentStart` | `webkit_user_content_manager_add_script()` | `AddScriptToExecuteOnDocumentCreated()` |

**Gotchas:**
- **WebKitGTK**: `get_http_body()` returns a `GInputStream*` — read asynchronously with `g_input_stream_read_async()`. Not available before WebKit 4.1.
- **WebView2**: `Request.Content` is a COM `IStream*` — read with `IStream::Read()`. Set `AllowedOrigins` or POST requests are CORS-blocked before they reach your handler.
- **WKWebView**: `httpBody` is a synchronous `NSData*` — straightforward, but `Content-Type` is not set automatically on the JS side if your handler inspects it.