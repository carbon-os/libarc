#include "WebViewImpl.hpp"
#include <gio/gio.h>
#include <climits>
#include <cstdlib>
#include <sstream>
#include <iomanip>
#include <random>
#include <unordered_map>

static const char kIpcBridgeJS[] = R"js(
(function () {
  'use strict';

  var _handlers     = {};
  var _listeners    = {};
  var _binHandlers  = {};
  var _binListeners = {};

  function ipcFetch(path, method, body) {
    return fetch('webview://app/__ipc__/' + path, {
      method: method,
      body:   body
    });
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
          var fn = _listeners[ch];
          if (fn) fn(JSON.parse(t));
        }).catch(function () {});
    },
    _deliverBinary: function (token, ch) {
      ipcFetch('pull/' + token, 'GET', null)
        .then(function (r) { return r.arrayBuffer(); })
        .then(function (buf) {
          var fn = _binListeners[ch];
          if (fn) fn(buf);
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
          window.webkit.messageHandlers.__wv_console__.postMessage(
            { level: lv, text: text });
        } catch (e) {}
      };
    });
  })();
})();
)js";

namespace webview {

static std::string gen_token() {
    static std::mt19937_64 rng(std::random_device{}());
    std::ostringstream ss;
    ss << std::hex << std::setw(16) << std::setfill('0') << rng();
    return ss.str();
}

static std::string js_str(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
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

static std::string ext_of(const std::string& path) {
    auto dot = path.rfind('.');
    if (dot == std::string::npos) return "";
    std::string ext = path.substr(dot + 1);
    for (auto& c : ext) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return ext;
}

static std::string mime_for_ext(const std::string& ext) {
    static const std::unordered_map<std::string, std::string> map = {
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

void WebViewImpl::install_ipc_bridge() {
    WebKitUserScript* script = webkit_user_script_new(
        kIpcBridgeJS,
        WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
        WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START,
        nullptr, nullptr);
    webkit_user_content_manager_add_script(ucm_, script);
    webkit_user_script_unref(script);
}

void WebViewImpl::on_uri_scheme_request(WebKitURISchemeRequest* request,
                                        gpointer user_data) {
    auto* self = static_cast<WebViewImpl*>(user_data);
    self->handle_scheme_ipc_task(request);
}

void WebViewImpl::handle_scheme_ipc_task(WebKitURISchemeRequest* request) {
    const gchar* path_raw = webkit_uri_scheme_request_get_path(request);
    std::string path = path_raw ? path_raw : "";
    if (!path.empty() && path[0] == '/') path = path.substr(1);

    // ── Shared response helpers ───────────────────────────────────────────────
    auto finish_bytes = [](WebKitURISchemeRequest* req,
                           const void* data, gsize sz, const char* mime) {
        GInputStream* stream = g_memory_input_stream_new_from_data(
            g_memdup(data, sz), sz, g_free);
        webkit_uri_scheme_request_finish(req, stream, (gint64)sz, mime);
        g_object_unref(stream);
    };

    auto finish_empty = [](WebKitURISchemeRequest* req) {
        GInputStream* stream = g_memory_input_stream_new();
        webkit_uri_scheme_request_finish(req, stream, 0, "application/octet-stream");
        g_object_unref(stream);
    };

    auto finish_error = [](WebKitURISchemeRequest* req, GIOErrorEnum code,
                           const std::string& msg) {
        GError* err = g_error_new(G_IO_ERROR, code, "%s", msg.c_str());
        webkit_uri_scheme_request_finish_error(req, err);
        g_error_free(err);
    };

    // ── webview://app/* — serve resource_root_ files or inline HTML ───────────
    static const std::string kIpcPrefix = "__ipc__/";
    if (path.rfind(kIpcPrefix, 0) != 0) {
        // url_path is whatever follows the leading slash, e.g. "assets/app.js"
        std::string url_path = path;
        if (url_path.empty()) url_path = "index.html";

        if (!resource_root_.empty()) {
            std::string full = resource_root_ + "/" + url_path;

            // Path traversal guard using realpath.
            char resolved[PATH_MAX]{};
            char root_res[PATH_MAX]{};
            realpath(full.c_str(),          resolved);
            realpath(resource_root_.c_str(), root_res);

            if (std::string(resolved).find(root_res) != 0) {
                finish_error(request, G_IO_ERROR_PERMISSION_DENIED, "Forbidden");
                return;
            }

            GError*      err    = nullptr;
            GMappedFile* mapped = g_mapped_file_new(resolved, FALSE, &err);
            if (!mapped) {
                webkit_uri_scheme_request_finish_error(request, err);
                g_error_free(err);
                return;
            }
            gsize       sz   = g_mapped_file_get_length(mapped);
            const void* data = g_mapped_file_get_contents(mapped);
            std::string mime = mime_for_ext(ext_of(resolved));
            finish_bytes(request, data, sz, mime.c_str());
            g_mapped_file_unref(mapped);
            return;
        }

        // Fallback: inline pending_html_ (load_html / load_file entry point).
        if (!pending_html_.empty()) {
            finish_bytes(request, pending_html_.data(), pending_html_.size(),
                         "text/html;charset=utf-8");
            return;
        }

        finish_error(request, G_IO_ERROR_NOT_FOUND, "No content loaded");
        return;
    }

    // ── webview://app/__ipc__/* — IPC transport ───────────────────────────────
    path = path.substr(kIpcPrefix.size());

    size_t slash = path.find('/');
    if (slash == std::string::npos) {
        finish_error(request, G_IO_ERROR_INVALID_ARGUMENT, "Malformed IPC path");
        return;
    }

    std::string action = path.substr(0, slash);
    char* unescaped = g_uri_unescape_string(path.substr(slash + 1).c_str(), nullptr);
    std::string arg = unescaped ? unescaped : "";
    if (unescaped) g_free(unescaped);

    // ── GET /pull/{token} ─────────────────────────────────────────────────────
    if (action == "pull") {
        auto it = pull_queue_.find(arg);
        if (it != pull_queue_.end()) {
            finish_bytes(request, it->second.data(), it->second.size(),
                         "application/octet-stream");
            pull_queue_.erase(it);
        } else {
            finish_error(request, G_IO_ERROR_NOT_FOUND, "Token not found");
        }
        return;
    }

    // ── Read body ─────────────────────────────────────────────────────────────
    GInputStream* body_stream =
        webkit_uri_scheme_request_get_http_body(request);
    std::vector<uint8_t> body_data;
    if (body_stream) {
        gsize bytes_read;
        char buf[8192];
        while (g_input_stream_read_all(body_stream, buf, sizeof(buf),
                                       &bytes_read, nullptr, nullptr)
               && bytes_read > 0) {
            body_data.insert(body_data.end(), buf, buf + bytes_read);
        }
    }

    // ── POST /send/{channel} ──────────────────────────────────────────────────
    if (action == "send") {
        std::string json_str(body_data.begin(), body_data.end());
        Message msg;
        msg.channel = arg;
        msg.body    = json::parse(json_str, nullptr, false);

        auto it = ipc_listeners_.find(arg);
        if (it != ipc_listeners_.end()) {
            it->second(msg);
        } else {
            auto wc = ipc_listeners_.find("*");
            if (wc != ipc_listeners_.end()) wc->second(msg);
        }
        finish_empty(request);
        return;
    }

    // ── POST /send-bin/{channel} ──────────────────────────────────────────────
    if (action == "send-bin") {
        BinaryMessage msg;
        msg.channel = arg;
        msg.data    = std::move(body_data);

        auto it = ipc_bin_listeners_.find(arg);
        if (it != ipc_bin_listeners_.end()) {
            it->second(msg);
        } else {
            auto wc = ipc_bin_listeners_.find("*");
            if (wc != ipc_bin_listeners_.end()) wc->second(msg);
        }
        finish_empty(request);
        return;
    }

    // ── POST /invoke/{channel} ────────────────────────────────────────────────
    if (action == "invoke") {
        auto it = ipc_handlers_.find(arg);
        if (it == ipc_handlers_.end()) {
            finish_error(request, G_IO_ERROR_NOT_FOUND, "No handler: " + arg);
            return;
        }
        std::string json_str(body_data.begin(), body_data.end());
        Message msg;
        msg.channel = arg;
        msg.body    = json::parse(json_str, nullptr, false);

        g_object_ref(request);
        msg.set_reply_fn([request, finish_bytes, finish_error](
                             json body, bool ok, std::string reason) {
            if (ok) {
                std::string s = body.dump();
                finish_bytes(request, s.data(), s.size(),
                             "application/json;charset=utf-8");
            } else {
                finish_error(request, G_IO_ERROR_FAILED, reason);
            }
            g_object_unref(request);
        });
        it->second(msg);
        return;
    }

    // ── POST /invoke-bin/{channel} ────────────────────────────────────────────
    if (action == "invoke-bin") {
        auto it = ipc_bin_handlers_.find(arg);
        if (it == ipc_bin_handlers_.end()) {
            finish_error(request, G_IO_ERROR_NOT_FOUND, "No handler: " + arg);
            return;
        }
        BinaryMessage msg;
        msg.channel = arg;
        msg.data    = std::move(body_data);

        g_object_ref(request);
        msg.set_reply_fn([request, finish_bytes, finish_error](
                             std::vector<uint8_t> data, bool ok,
                             std::string reason) {
            if (ok) {
                finish_bytes(request, data.data(), data.size(),
                             "application/octet-stream");
            } else {
                finish_error(request, G_IO_ERROR_FAILED, reason);
            }
            g_object_unref(request);
        });
        it->second(msg);
        return;
    }

    // ── POST /reply/{token} ───────────────────────────────────────────────────
    if (action == "reply") {
        auto it = pending_host_invokes_.find(arg);
        if (it != pending_host_invokes_.end()) {
            std::string json_str(body_data.begin(), body_data.end());
            bool is_reject = json_str.rfind("__reject__:", 0) == 0;
            Message msg;
            msg.channel = "";
            msg.body    = is_reject ? json(nullptr)
                                    : json::parse(json_str, nullptr, false);
            auto fn = std::move(it->second.fn);
            pending_host_invokes_.erase(it);
            fn(msg);
        }
        finish_empty(request);
        return;
    }

    // ── POST /reply-bin/{token} ───────────────────────────────────────────────
    if (action == "reply-bin") {
        auto it = pending_host_bin_invokes_.find(arg);
        if (it != pending_host_bin_invokes_.end()) {
            BinaryMessage msg;
            msg.channel = "";
            msg.data    = std::move(body_data);
            auto fn = std::move(it->second.fn);
            pending_host_bin_invokes_.erase(it);
            fn(msg);
        }
        finish_empty(request);
        return;
    }

    finish_error(request, G_IO_ERROR_INVALID_ARGUMENT, "Unknown action: " + action);
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
    std::string tok = gen_token();
    std::string s   = body.dump();
    pull_queue_[tok] = std::vector<uint8_t>(s.begin(), s.end());
    eval_js_raw("window.ipc._deliver(" + js_str(tok) + "," + js_str(ch) + ")");
}

void WebViewImpl::ipc_invoke(const std::string& ch, const json& body,
                             std::function<void(Message&)> fn) {
    std::string tok = gen_token();
    std::string s   = body.dump();
    pull_queue_[tok]           = std::vector<uint8_t>(s.begin(), s.end());
    pending_host_invokes_[tok] = { std::move(fn) };
    eval_js_raw("window.ipc._invoke(" + js_str(tok) + "," + js_str(ch) + ")");
}

void WebViewImpl::ipc_send_binary(const std::string& ch,
                                  std::vector<uint8_t> data) {
    std::string tok = gen_token();
    pull_queue_[tok] = std::move(data);
    eval_js_raw("window.ipc._deliverBinary(" + js_str(tok) + "," +
                js_str(ch) + ")");
}

void WebViewImpl::ipc_invoke_binary(const std::string& ch,
                                    std::vector<uint8_t> data,
                                    std::function<void(BinaryMessage&)> fn) {
    std::string tok = gen_token();
    pull_queue_[tok]               = std::move(data);
    pending_host_bin_invokes_[tok] = { std::move(fn) };
    eval_js_raw("window.ipc._invokeBinary(" + js_str(tok) + "," +
                js_str(ch) + ")");
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