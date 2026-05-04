// ipc.mm
#import "WebViewImpl.hpp"
#include <webview/webview.hpp>
#include <string>
#include <sstream>
#include <random>
#include <iomanip>
#include <vector>

static const char kIpcBridgeJS[] = R"js(
(function () {
  'use strict';

  var _handlers     = {};
  var _listeners    = {};
  var _binHandlers  = {};
  var _binListeners = {};

  function ipcFetch(path, method, body) {
    return fetch('webview://ipc/' + path, {
      method:  method,
      body:    body
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

static NSData* read_body(id<WKURLSchemeTask> task) {
    NSData* body = task.request.HTTPBody;
    if (!body && task.request.HTTPBodyStream) {
        NSInputStream* s = task.request.HTTPBodyStream;
        [s open];
        NSMutableData* d = [NSMutableData data];
        uint8_t buf[8192];
        NSInteger n;
        while ((n = [s read:buf maxLength:sizeof(buf)]) > 0)
            [d appendBytes:buf length:(NSUInteger)n];
        [s close];
        body = d;
    }
    return body ?: [NSData data];
}

static void task_respond(id<WKURLSchemeTask> task,
                         int status, NSData* body, NSString* mime) {
    NSDictionary* hdrs = @{
        @"Content-Type":                mime ?: @"application/octet-stream",
        @"Access-Control-Allow-Origin": @"*",
        @"Cache-Control":               @"no-store"
    };
    NSHTTPURLResponse* resp =
        [[NSHTTPURLResponse alloc] initWithURL:task.request.URL
                                    statusCode:status
                                   HTTPVersion:@"HTTP/1.1"
                                  headerFields:hdrs];
    @try {
        [task didReceiveResponse:resp];
        if (body.length > 0) [task didReceiveData:body];
        [task didFinish];
    } @catch (NSException*) {}
}

static void task_ok_empty(id<WKURLSchemeTask> task) {
    task_respond(task, 204, nil, @"application/octet-stream");
}

static void task_ok_json(id<WKURLSchemeTask> task, const std::string& json_str) {
    NSData* d = [NSData dataWithBytes:json_str.data() length:json_str.size()];
    task_respond(task, 200, d, @"application/json;charset=utf-8");
}

static void task_ok_bytes(id<WKURLSchemeTask> task,
                          const std::vector<uint8_t>& data) {
    NSData* d = [NSData dataWithBytes:data.data() length:data.size()];
    task_respond(task, 200, d, @"application/octet-stream");
}

static void task_error(id<WKURLSchemeTask> task, int status,
                       const std::string& msg) {
    NSData* d = [NSData dataWithBytes:msg.data() length:msg.size()];
    task_respond(task, status, d, @"text/plain;charset=utf-8");
}

void WebViewImpl::install_ipc_bridge() {
    WKUserScript* script =
        [[WKUserScript alloc]
              initWithSource:@(kIpcBridgeJS)
               injectionTime:WKUserScriptInjectionTimeAtDocumentStart
            forMainFrameOnly:NO];
    [wk_config_.userContentController addUserScript:script];
}

void WebViewImpl::handle_scheme_ipc_task(void* raw_task) {
    id<WKURLSchemeTask> task = (__bridge id<WKURLSchemeTask>)raw_task;

    NSURL*    url    = task.request.URL;
    NSString* method = [task.request.HTTPMethod uppercaseString];

    NSString* path = url.path ?: @"";
    if ([path hasPrefix:@"/"]) path = [path substringFromIndex:1];

    NSRange slash = [path rangeOfString:@"/"];
    if (slash.location == NSNotFound) {
        task_error(task, 400, "malformed IPC path");
        return;
    }
    NSString* action = [path substringToIndex:slash.location];
    NSString* argNS  = [[path substringFromIndex:slash.location + 1]
                            stringByRemovingPercentEncoding] ?: @"";
    std::string arg  = argNS.UTF8String ?: "";

    if ([action isEqualToString:@"pull"] && [method isEqualToString:@"GET"]) {
        auto it = pull_queue_.find(arg);
        if (it == pull_queue_.end()) {
            task_error(task, 404, "token not found: " + arg);
            return;
        }
        NSData* data = [NSData dataWithBytes:it->second.data()
                                      length:it->second.size()];
        pull_queue_.erase(it);
        task_respond(task, 200, data, @"application/octet-stream");
        return;
    }

    NSData*        body  = read_body(task);
    const uint8_t* bptr  = (const uint8_t*)body.bytes;
    const size_t   blen  = body.length;

    if ([action isEqualToString:@"send"]) {
        std::string json_str(bptr, bptr + blen);
        Message msg;
        msg.channel = arg;
        msg.body    = json::parse(json_str, nullptr, false);

        // Exact channel match first, then wildcard catch-all.
        auto it = ipc_listeners_.find(arg);
        if (it != ipc_listeners_.end()) {
            it->second(msg);
        } else {
            auto wc = ipc_listeners_.find("*");
            if (wc != ipc_listeners_.end())
                wc->second(msg);
        }
        task_ok_empty(task);
        return;
    }

    if ([action isEqualToString:@"send-bin"]) {
        BinaryMessage msg;
        msg.channel = arg;
        msg.data    = std::vector<uint8_t>(bptr, bptr + blen);

        auto it = ipc_bin_listeners_.find(arg);
        if (it != ipc_bin_listeners_.end()) {
            it->second(msg);
        } else {
            auto wc = ipc_bin_listeners_.find("*");
            if (wc != ipc_bin_listeners_.end())
                wc->second(msg);
        }
        task_ok_empty(task);
        return;
    }

    if ([action isEqualToString:@"invoke"]) {
        auto it = ipc_handlers_.find(arg);
        if (it == ipc_handlers_.end()) {
            task_error(task, 404, "No handler: " + arg);
            return;
        }
        std::string tok = gen_token();
        [held_invoke_tasks_ setObject:task forKey:@(tok.c_str())];

        std::string json_str(bptr, bptr + blen);
        Message msg;
        msg.channel = arg;
        msg.body    = json::parse(json_str, nullptr, false);

        auto* self = this;
        msg.set_reply_fn([self, tok](json body, bool ok, std::string reason) {
            id<WKURLSchemeTask> t =
                [self->held_invoke_tasks_ objectForKey:@(tok.c_str())];
            [self->held_invoke_tasks_ removeObjectForKey:@(tok.c_str())];
            if (!t) return;
            if (ok) task_ok_json(t, body.dump());
            else    task_error(t, 500, reason);
        });
        it->second(msg);
        return;
    }

    if ([action isEqualToString:@"invoke-bin"]) {
        auto it = ipc_bin_handlers_.find(arg);
        if (it == ipc_bin_handlers_.end()) {
            task_error(task, 404, "No handler: " + arg);
            return;
        }
        std::string tok = gen_token();
        [held_bin_invoke_tasks_ setObject:task forKey:@(tok.c_str())];

        BinaryMessage msg;
        msg.channel = arg;
        msg.data    = std::vector<uint8_t>(bptr, bptr + blen);

        auto* self = this;
        msg.set_reply_fn([self, tok](std::vector<uint8_t> data, bool ok, std::string reason) {
            id<WKURLSchemeTask> t =
                [self->held_bin_invoke_tasks_ objectForKey:@(tok.c_str())];
            [self->held_bin_invoke_tasks_ removeObjectForKey:@(tok.c_str())];
            if (!t) return;
            if (ok) task_ok_bytes(t, data);
            else    task_error(t, 500, reason);
        });
        it->second(msg);
        return;
    }

    if ([action isEqualToString:@"reply"]) {
        auto it = pending_host_invokes_.find(arg);
        if (it != pending_host_invokes_.end()) {
            std::string json_str(bptr, bptr + blen);
            bool is_reject = json_str.rfind("__reject__:", 0) == 0;
            Message msg;
            msg.channel = "";
            msg.body    = is_reject ? json(nullptr)
                                    : json::parse(json_str, nullptr, false);
            auto fn = std::move(it->second.fn);
            pending_host_invokes_.erase(it);
            fn(msg);
        }
        task_ok_empty(task);
        return;
    }

    if ([action isEqualToString:@"reply-bin"]) {
        auto it = pending_host_bin_invokes_.find(arg);
        if (it != pending_host_bin_invokes_.end()) {
            BinaryMessage msg;
            msg.channel = "";
            msg.data    = std::vector<uint8_t>(bptr, bptr + blen);
            auto fn = std::move(it->second.fn);
            pending_host_bin_invokes_.erase(it);
            fn(msg);
        }
        task_ok_empty(task);
        return;
    }

    task_error(task, 400, "unknown IPC action: " + std::string(action.UTF8String ?: ""));
}

void WebViewImpl::on_scheme_task_stopped(void* raw_task) {
    id<WKURLSchemeTask> task = (__bridge id<WKURLSchemeTask>)raw_task;
    for (NSMutableDictionary* dict in @[ held_invoke_tasks_, held_bin_invoke_tasks_ ]) {
        NSMutableArray* remove = [NSMutableArray array];
        [dict enumerateKeysAndObjectsUsingBlock:
            ^(NSString* k, id<WKURLSchemeTask> t, BOOL*) {
                if (t == task) [remove addObject:k];
            }];
        [dict removeObjectsForKeys:remove];
    }
}

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

void WebViewImpl::ipc_send_binary(const std::string& ch,
                                  std::vector<uint8_t> data) {
    std::string tok = gen_token();
    pull_queue_[tok] = std::move(data);
    eval_js_raw("window.ipc._deliverBinary(" + js_str(tok) + "," + js_str(ch) + ")");
}

void WebViewImpl::ipc_invoke(const std::string& ch, const json& body,
                             std::function<void(Message&)> fn) {
    std::string tok = gen_token();
    std::string s   = body.dump();
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