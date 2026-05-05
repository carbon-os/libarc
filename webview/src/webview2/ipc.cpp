#include "WebViewImpl.hpp"
#include "string_util.hpp"

#include <webview/webview.hpp>
#include <shlwapi.h>

#include <sstream>
#include <random>
#include <iomanip>
#include <fstream>
#include <map>

static const char kIpcBridgeJS[] = R"js(
(function () {
  'use strict';

  var _handlers     = {};
  var _listeners    = {};
  var _binHandlers  = {};
  var _binListeners = {};

  function ipcFetch(path, method, body) {
    return fetch('webview://ipc/' + path, { method: method, body: body });
  }

  var ipc = {
    send: function (ch, body) {
      ipcFetch('send/' + encodeURIComponent(ch), 'POST',
        new TextEncoder().encode(JSON.stringify(body !== undefined ? body : null)));
    },
    invoke: function (ch, body) {
      return ipcFetch('invoke/' + encodeURIComponent(ch), 'POST',
          new TextEncoder().encode(JSON.stringify(body !== undefined ? body : null)))
        .then(function (r) {
          if (!r.ok) return r.text().then(function (t) { throw new Error(t); });
          return r.text();
        })
        .then(function (t) { return JSON.parse(t); });
    },
    sendBinary: function (ch, buf) {
      ipcFetch('send-bin/' + encodeURIComponent(ch), 'POST', buf);
    },
    invokeBinary: function (ch, buf) {
      return ipcFetch('invoke-bin/' + encodeURIComponent(ch), 'POST', buf)
        .then(function (r) {
          if (!r.ok) return r.text().then(function (t) { throw new Error(t); });
          return r.arrayBuffer();
        });
    },
    handle:       function (ch, fn) { _handlers[ch]     = fn; },
    on:           function (ch, fn) { _listeners[ch]    = fn; },
    off:          function (ch)     { delete _listeners[ch];  },
    handleBinary: function (ch, fn) { _binHandlers[ch]  = fn; },
    onBinary:     function (ch, fn) { _binListeners[ch] = fn; },
    offBinary:    function (ch)     { delete _binListeners[ch]; },

    _deliver: function (token, ch) {
      ipcFetch('pull/' + token, 'GET', null)
        .then(function (r) { return r.text(); })
        .then(function (t) {
          var fn = _listeners[ch]; if (fn) fn(JSON.parse(t));
        }).catch(function () {});
    },
    _deliverBinary: function (token, ch) {
      ipcFetch('pull/' + token, 'GET', null)
        .then(function (r) { return r.arrayBuffer(); })
        .then(function (buf) {
          var fn = _binListeners[ch]; if (fn) fn(buf);
        }).catch(function () {});
    },
    _invoke: function (token, ch) {
      ipcFetch('pull/' + token, 'GET', null)
        .then(function (r) { return r.text(); })
        .then(function (t) {
          var h = _handlers[ch];
          if (!h) {
            return ipcFetch('reply/' + token, 'POST',
              new TextEncoder().encode('__reject__:No handler: ' + ch));
          }
          return Promise.resolve(h(JSON.parse(t)))
            .then(function (result) {
              return ipcFetch('reply/' + token, 'POST',
                new TextEncoder().encode(
                  JSON.stringify(result !== undefined ? result : null)));
            })
            .catch(function (e) {
              return ipcFetch('reply/' + token, 'POST',
                new TextEncoder().encode('__reject__:' + String(e)));
            });
        }).catch(function () {});
    },
    _invokeBinary: function (token, ch) {
      ipcFetch('pull/' + token, 'GET', null)
        .then(function (r) { return r.arrayBuffer(); })
        .then(function (buf) {
          var h = _binHandlers[ch];
          if (!h) {
            return ipcFetch('reply-bin/' + token, 'POST', new Uint8Array(0).buffer);
          }
          return Promise.resolve(h(buf))
            .then(function (result) {
              return ipcFetch('reply-bin/' + token, 'POST',
                result instanceof ArrayBuffer ? result : new Uint8Array(0).buffer);
            })
            .catch(function () {
              return ipcFetch('reply-bin/' + token, 'POST', new Uint8Array(0).buffer);
            });
        }).catch(function () {});
    }
  };

  window.ipc = ipc;

  (function () {
    var levels = ['log', 'info', 'warn', 'error'];
    levels.forEach(function (lv) {
      var orig = console[lv].bind(console);
      console[lv] = function () {
        orig.apply(console, arguments);
        try {
          var text = Array.prototype.slice.call(arguments).map(function (a) {
            return (typeof a === 'object') ? JSON.stringify(a) : String(a);
          }).join(' ');
          fetch('webview://ipc/__console__/', {
            method: 'POST',
            body: JSON.stringify({ level: lv, text: text })
          });
        } catch(e) {}
      };
    });
  })();
})();
)js";

namespace webview {

using namespace detail;
using namespace Microsoft::WRL;

static std::string gen_token() {
    static std::mt19937_64 rng(std::random_device{}());
    std::ostringstream ss;
    ss << std::hex << std::setw(16) << std::setfill('0') << rng();
    return ss.str();
}

static std::string js_str(const std::string& s) {
    std::string out;
    out += '\'';
    for (char c : s) {
        if      (c == '\'') out += "\\'";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else                out += c;
    }
    out += '\'';
    return out;
}

static std::string mime_for_ext(const std::string& ext) {
    static const std::map<std::string, std::string> map = {
        {"html",  "text/html;charset=utf-8"},
        {"htm",   "text/html;charset=utf-8"},
        {"js",    "application/javascript"},
        {"mjs",   "application/javascript"},
        {"css",   "text/css"},
        {"json",  "application/json"},
        {"wasm",  "application/wasm"},
        {"png",   "image/png"},
        {"jpg",   "image/jpeg"},
        {"jpeg",  "image/jpeg"},
        {"gif",   "image/gif"},
        {"webp",  "image/webp"},
        {"svg",   "image/svg+xml"},
        {"ico",   "image/x-icon"},
        {"ttf",   "font/ttf"},
        {"woff",  "font/woff"},
        {"woff2", "font/woff2"},
        {"txt",   "text/plain;charset=utf-8"},
        {"xml",   "application/xml"},
        {"mp4",   "video/mp4"},
        {"webm",  "video/webm"},
    };
    auto it = map.find(ext);
    return it != map.end() ? it->second : "application/octet-stream";
}

static std::string ext_of(const std::string& path) {
    auto dot = path.rfind('.');
    if (dot == std::string::npos) return "";
    std::string ext = path.substr(dot + 1);
    for (auto& c : ext) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return ext;
}

static HRESULT make_response(
    ICoreWebView2Environment* env,
    int status, const std::wstring& reason,
    const std::vector<uint8_t>& body,
    const std::wstring& headers,
    ComPtr<ICoreWebView2WebResourceResponse>& out)
{
    IStream* stream = nullptr;
    if (!body.empty())
        stream = SHCreateMemStream(body.data(), (UINT)body.size());

    return env->CreateWebResourceResponse(
        stream, status, reason.c_str(), headers.c_str(), &out);
}

static void respond(ICoreWebView2WebResourceRequestedEventArgs* args,
                    ICoreWebView2Deferral* deferral,
                    ICoreWebView2Environment* env,
                    int status, const std::vector<uint8_t>& body,
                    const std::string& mime)
{
    std::map<std::string, std::string> extra;
    ComPtr<ICoreWebView2WebResourceResponse> resp;
    make_response(env, status, status < 400 ? L"OK" : L"Error",
                  body, make_response_headers(status, mime, extra), resp);
    args->put_Response(resp.Get());
    if (deferral) deferral->Complete();
}

static void respond_empty(ICoreWebView2WebResourceRequestedEventArgs* args,
                          ICoreWebView2Deferral* deferral,
                          ICoreWebView2Environment* env)
{
    respond(args, deferral, env, 204, {}, "application/octet-stream");
}

static void respond_json(ICoreWebView2WebResourceRequestedEventArgs* args,
                         ICoreWebView2Deferral* deferral,
                         ICoreWebView2Environment* env,
                         const std::string& json_str)
{
    std::vector<uint8_t> body(json_str.begin(), json_str.end());
    respond(args, deferral, env, 200, body, "application/json;charset=utf-8");
}

static void respond_bytes(ICoreWebView2WebResourceRequestedEventArgs* args,
                          ICoreWebView2Deferral* deferral,
                          ICoreWebView2Environment* env,
                          const std::vector<uint8_t>& data)
{
    respond(args, deferral, env, 200, data, "application/octet-stream");
}

static void respond_error(ICoreWebView2WebResourceRequestedEventArgs* args,
                          ICoreWebView2Deferral* deferral,
                          ICoreWebView2Environment* env,
                          int status, const std::string& msg)
{
    std::vector<uint8_t> body(msg.begin(), msg.end());
    respond(args, deferral, env, status, body, "text/plain;charset=utf-8");
}

// ── Bridge installation ───────────────────────────────────────────────────────

void WebViewImpl::install_ipc_bridge() {
    bool done = false;
    webview_->AddScriptToExecuteOnDocumentCreated(
        to_wide(kIpcBridgeJS).c_str(),
        Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>(
            [this, &done](HRESULT, LPCWSTR id) -> HRESULT {
                if (id) user_script_ids_.emplace_back(id);
                done = true;
                return S_OK;
            }).Get());

    MSG msg;
    for (int i = 0; i < 500 && !done; ++i) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        } else {
            Sleep(1);
        }
    }
}

// ── Main WebResourceRequested dispatcher ──────────────────────────────────────

void WebViewImpl::dispatch_web_resource_request(
    ICoreWebView2WebResourceRequestedEventArgs* args)
{
    ComPtr<ICoreWebView2WebResourceRequest> req;
    args->get_Request(&req);

    CoStr url_cs; req->get_Uri(&url_cs.ptr);
    std::string url = url_cs.str();

    CoStr method_cs; req->get_Method(&method_cs.ptr);
    std::string method = method_cs.str();

    // ── webview://ipc/* — IPC transport ───────────────────────────────────────
    if (url.rfind("webview://ipc/", 0) == 0) {
        ComPtr<IStream> body_stream;
        req->get_Content(&body_stream);
        std::vector<uint8_t> body_bytes =
            body_stream ? read_istream(body_stream.Get()) : std::vector<uint8_t>{};

        std::string path = url.substr(strlen("webview://ipc/"));
        auto slash = path.find('/');
        std::string action = (slash != std::string::npos)
            ? path.substr(0, slash) : path;
        std::string arg = (slash != std::string::npos)
            ? path.substr(slash + 1) : "";

        if (!arg.empty() && arg.back() == '/') arg.pop_back();

        std::string decoded_arg;
        for (size_t i = 0; i < arg.size(); ++i) {
            if (arg[i] == '%' && i + 2 < arg.size()) {
                int val = 0;
                sscanf_s(arg.c_str() + i + 1, "%2x", &val);
                decoded_arg += static_cast<char>(val);
                i += 2;
            } else {
                decoded_arg += arg[i];
            }
        }
        arg = decoded_arg;

        ComPtr<ICoreWebView2Deferral> deferral;
        bool needs_deferral = (action == "invoke" || action == "invoke-bin");
        if (needs_deferral) args->GetDeferral(&deferral);

        if (action == "pull" && method == "GET") {
            auto it = pull_queue_.find(arg);
            if (it == pull_queue_.end()) {
                respond_error(args, nullptr, environment_.Get(), 404,
                              "token not found: " + arg);
            } else {
                auto data = std::move(it->second);
                pull_queue_.erase(it);
                respond_bytes(args, nullptr, environment_.Get(), data);
            }
            return;
        }

        if (action == "send") {
            std::string json_str(body_bytes.begin(), body_bytes.end());
            Message msg;
            msg.channel = arg;
            msg.body    = json::parse(json_str, nullptr, false);

            auto it = ipc_listeners_.find(arg);
            if (it != ipc_listeners_.end()) {
                it->second(msg);
            } else {
                auto wc = ipc_listeners_.find("*");
                if (wc != ipc_listeners_.end())
                    wc->second(msg);
            }
            respond_empty(args, nullptr, environment_.Get());
            return;
        }

        if (action == "send-bin") {
            BinaryMessage msg;
            msg.channel = arg;
            msg.data    = body_bytes;

            auto it = ipc_bin_listeners_.find(arg);
            if (it != ipc_bin_listeners_.end()) {
                it->second(msg);
            } else {
                auto wc = ipc_bin_listeners_.find("*");
                if (wc != ipc_bin_listeners_.end())
                    wc->second(msg);
            }
            respond_empty(args, nullptr, environment_.Get());
            return;
        }

        if (action == "invoke") {
            auto it = ipc_handlers_.find(arg);
            if (it == ipc_handlers_.end()) {
                respond_error(args, deferral.Get(), environment_.Get(),
                              404, "No handler: " + arg);
                return;
            }
            std::string tok = gen_token();
            {
                PendingRendererInvoke pending;
                pending.deferral  = deferral;
                pending.ev_args   = args;
                pending_renderer_invokes_.emplace(tok, std::move(pending));
            }
            std::string json_str(body_bytes.begin(), body_bytes.end());
            Message msg;
            msg.channel = arg;
            msg.body    = json::parse(json_str, nullptr, false);

            msg.set_reply_fn([this, tok](json body, bool ok, std::string reason) {
                auto it = pending_renderer_invokes_.find(tok);
                if (it == pending_renderer_invokes_.end()) return;
                auto pending = std::move(it->second);
                pending_renderer_invokes_.erase(it);
                if (ok) respond_json(pending.ev_args.Get(), pending.deferral.Get(),
                                     environment_.Get(), body.dump());
                else    respond_error(pending.ev_args.Get(), pending.deferral.Get(),
                                      environment_.Get(), 500, reason);
            });
            it->second(msg);
            return;
        }

        if (action == "invoke-bin") {
            auto it = ipc_bin_handlers_.find(arg);
            if (it == ipc_bin_handlers_.end()) {
                respond_error(args, deferral.Get(), environment_.Get(),
                              404, "No handler: " + arg);
                return;
            }
            std::string tok = gen_token();
            {
                PendingRendererInvoke pending;
                pending.deferral = deferral;
                pending.ev_args  = args;
                pending_renderer_invokes_.emplace(tok, std::move(pending));
            }
            BinaryMessage msg;
            msg.channel = arg;
            msg.data    = body_bytes;

            msg.set_reply_fn([this, tok](std::vector<uint8_t> data, bool ok,
                                         std::string reason) {
                auto it = pending_renderer_invokes_.find(tok);
                if (it == pending_renderer_invokes_.end()) return;
                auto pending = std::move(it->second);
                pending_renderer_invokes_.erase(it);
                if (ok) respond_bytes(pending.ev_args.Get(), pending.deferral.Get(),
                                      environment_.Get(), data);
                else    respond_error(pending.ev_args.Get(), pending.deferral.Get(),
                                      environment_.Get(), 500, reason);
            });
            it->second(msg);
            return;
        }

        if (action == "reply") {
            auto it = pending_host_invokes_.find(arg);
            if (it != pending_host_invokes_.end()) {
                std::string json_str(body_bytes.begin(), body_bytes.end());
                bool is_reject = (json_str.rfind("__reject__:", 0) == 0);
                Message msg;
                msg.body = is_reject ? json(nullptr)
                                     : json::parse(json_str, nullptr, false);
                auto fn = std::move(it->second.fn);
                pending_host_invokes_.erase(it);
                fn(msg);
            }
            respond_empty(args, nullptr, environment_.Get());
            return;
        }

        if (action == "reply-bin") {
            auto it = pending_host_bin_invokes_.find(arg);
            if (it != pending_host_bin_invokes_.end()) {
                BinaryMessage msg;
                msg.data = body_bytes;
                auto fn = std::move(it->second.fn);
                pending_host_bin_invokes_.erase(it);
                fn(msg);
            }
            respond_empty(args, nullptr, environment_.Get());
            return;
        }

        if (action == "__console__") {
            if (on_console_cb) {
                std::string json_str(body_bytes.begin(), body_bytes.end());
                auto j = json::parse(json_str, nullptr, false);
                if (!j.is_discarded()) {
                    ConsoleMessage cm;
                    std::string level = j.value("level", "log");
                    cm.text = j.value("text", "");
                    if      (level == "info")  cm.level = ConsoleLevel::Info;
                    else if (level == "warn")  cm.level = ConsoleLevel::Warn;
                    else if (level == "error") cm.level = ConsoleLevel::Error;
                    else                       cm.level = ConsoleLevel::Log;
                    on_console_cb(cm);
                }
            }
            respond_empty(args, nullptr, environment_.Get());
            return;
        }

        respond_error(args, nullptr, environment_.Get(), 400,
                      "unknown IPC action: " + action);
        return;
    }

    // ── webview://app/* — serve resource_root_ files or inline HTML ───────────
    if (url.rfind("webview://app", 0) == 0) {
        // Extract the URL path component.
        std::string url_path = url.substr(strlen("webview://app"));
        if (url_path.empty() || url_path == "/") url_path = "/index.html";

        if (!resource_root_.empty()) {
            // Build the full filesystem path (convert forward slashes).
            std::string full = resource_root_;
            for (char c : url_path)
                full += (c == '/') ? '\\' : c;

            // Path traversal guard.
            wchar_t resolved[MAX_PATH]{};
            wchar_t root_res[MAX_PATH]{};
            GetFullPathNameW(to_wide(full).c_str(),         MAX_PATH, resolved, nullptr);
            GetFullPathNameW(to_wide(resource_root_).c_str(), MAX_PATH, root_res, nullptr);

            if (std::wstring(resolved).find(root_res) != 0) {
                respond_error(args, nullptr, environment_.Get(), 403, "Forbidden");
                return;
            }

            std::ifstream f(resolved, std::ios::binary);
            if (!f) {
                respond_error(args, nullptr, environment_.Get(), 404, "Not found");
                return;
            }
            std::vector<uint8_t> body(std::istreambuf_iterator<char>(f), {});
            std::string mime = mime_for_ext(ext_of(to_utf8(resolved)));
            respond(args, nullptr, environment_.Get(), 200, body, mime);
            return;
        }

        // Fallback: serve inline pending_html_.
        std::vector<uint8_t> body(pending_html_.begin(), pending_html_.end());
        respond(args, nullptr, environment_.Get(), 200, body,
                "text/html;charset=utf-8");
        return;
    }

    // ── All other requests → on_request_cb ───────────────────────────────────
    if (on_request_cb) {
        ResourceRequest rreq;
        rreq.url    = url;
        rreq.method = method;

        ComPtr<ICoreWebView2HttpRequestHeaders> hdrs;
        req->get_Headers(&hdrs);
        if (hdrs) {
            ComPtr<ICoreWebView2HttpHeadersCollectionIterator> it;
            hdrs->GetIterator(&it);
            BOOL has = FALSE;
            while (SUCCEEDED(it->get_HasCurrentHeader(&has)) && has) {
                CoStr k, v;
                it->GetCurrentHeader(&k.ptr, &v.ptr);
                rreq.headers[k.str()] = v.str();
                BOOL moved = FALSE;
                it->MoveNext(&moved);
                if (!moved) break;
            }
        }

        auto accept_it = rreq.headers.find("Accept");
        if (accept_it != rreq.headers.end()) {
            const auto& accept = accept_it->second;
            if (accept.find("text/html") != std::string::npos)
                rreq.resource_type = ResourceType::Document;
            else if (accept.find("javascript") != std::string::npos)
                rreq.resource_type = ResourceType::Script;
            else if (accept.find("image") != std::string::npos)
                rreq.resource_type = ResourceType::Image;
        }

        on_request_cb(rreq);

        switch (rreq.action()) {
            case ResourceRequest::Action::Cancel: {
                ComPtr<ICoreWebView2WebResourceResponse> resp;
                environment_->CreateWebResourceResponse(
                    nullptr, 0, L"Blocked", L"", &resp);
                args->put_Response(resp.Get());
                return;
            }
            case ResourceRequest::Action::Redirect: {
                eval_js_raw("location.replace(" +
                            json(rreq.redirect_url()).dump() + ")");
                return;
            }
            case ResourceRequest::Action::Respond: {
                const auto& res = rreq.response();
                ComPtr<ICoreWebView2WebResourceResponse> resp;
                std::wstring hdrs_str;
                for (auto& [k, v] : res.headers)
                    hdrs_str += to_wide(k) + L": " + to_wide(v) + L"\r\n";
                make_response(environment_.Get(),
                              res.status, L"OK", res.body, hdrs_str, resp);
                args->put_Response(resp.Get());
                return;
            }
            default:
                break;
        }
    }
}

// ── ipc_* implementations ─────────────────────────────────────────────────────

void WebViewImpl::ipc_handle(const std::string& ch,
                             std::function<void(Message&)> fn)
    { ipc_handlers_[ch] = std::move(fn); }

void WebViewImpl::ipc_on(const std::string& ch,
                         std::function<void(Message&)> fn)
    { ipc_listeners_[ch] = std::move(fn); }

void WebViewImpl::ipc_handle_binary(const std::string& ch,
                                    std::function<void(BinaryMessage&)> fn)
    { ipc_bin_handlers_[ch] = std::move(fn); }

void WebViewImpl::ipc_on_binary(const std::string& ch,
                                std::function<void(BinaryMessage&)> fn)
    { ipc_bin_listeners_[ch] = std::move(fn); }

void WebViewImpl::ipc_send(const std::string& ch, const json& body) {
    std::string s = body.dump();
    std::string tok = gen_token();
    pull_queue_[tok] = std::vector<uint8_t>(s.begin(), s.end());
    eval_js_raw("window.ipc._deliver(" + js_str(tok) + "," + js_str(ch) + ")");
}

void WebViewImpl::ipc_send_binary(const std::string& ch,
                                  std::vector<uint8_t> data) {
    std::string tok = gen_token();
    pull_queue_[tok] = std::move(data);
    eval_js_raw("window.ipc._deliverBinary(" + js_str(tok) + "," + js_str(ch) + ")");
}

void WebViewImpl::ipc_invoke(const std::string& ch, const json& body,
                             std::function<void(Message&)> fn) {
    std::string s = body.dump();
    std::string tok = gen_token();
    pull_queue_[tok]           = std::vector<uint8_t>(s.begin(), s.end());
    pending_host_invokes_[tok] = { std::move(fn) };
    eval_js_raw("window.ipc._invoke(" + js_str(tok) + "," + js_str(ch) + ")");
}

void WebViewImpl::ipc_invoke_binary(const std::string& ch,
                                    std::vector<uint8_t> data,
                                    std::function<void(BinaryMessage&)> fn) {
    std::string tok = gen_token();
    pull_queue_[tok]               = std::move(data);
    pending_host_bin_invokes_[tok] = { std::move(fn) };
    eval_js_raw("window.ipc._invokeBinary(" + js_str(tok) + "," + js_str(ch) + ")");
}

void Message::reply(json payload) {
    if (reply_fn_) reply_fn_(std::move(payload), true, "");
}
void Message::reject(std::string reason) {
    if (reply_fn_) reply_fn_({}, false, std::move(reason));
}

void BinaryMessage::reply(std::vector<uint8_t> payload) {
    if (reply_fn_) reply_fn_(std::move(payload), true, "");
}
void BinaryMessage::reject(std::string reason) {
    if (reply_fn_) reply_fn_({}, false, std::move(reason));
}

void IPC::handle(const std::string& ch, std::function<void(Message&)> fn)
    { impl_->ipc_handle(ch, std::move(fn)); }

void IPC::on(const std::string& ch, std::function<void(Message&)> fn)
    { impl_->ipc_on(ch, std::move(fn)); }

void IPC::send(const std::string& ch, json body)
    { impl_->ipc_send(ch, body); }

void IPC::invoke(const std::string& ch, json body,
                 std::function<void(Message&)> fn)
    { impl_->ipc_invoke(ch, body, std::move(fn)); }

void IPC::handle(const std::string& ch, std::function<void(BinaryMessage&)> fn)
    { impl_->ipc_handle_binary(ch, std::move(fn)); }

void IPC::on_binary(const std::string& ch, std::function<void(BinaryMessage&)> fn)
    { impl_->ipc_on_binary(ch, std::move(fn)); }

void IPC::send_binary(const std::string& ch, std::vector<uint8_t> data)
    { impl_->ipc_send_binary(ch, std::move(data)); }

void IPC::invoke(const std::string& ch, std::vector<uint8_t> data,
                 std::function<void(BinaryMessage&)> fn)
    { impl_->ipc_invoke_binary(ch, std::move(data), std::move(fn)); }

} // namespace webview