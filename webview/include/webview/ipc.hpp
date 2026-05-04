#pragma once
#include "script.hpp"   // pulls in json typedef
#include <string>
#include <vector>
#include <functional>
#include <cstdint>

namespace webview {

// ── Message types ─────────────────────────────────────────────────────────────

class Message {
public:
    json        body;
    std::string channel;

    void reply(json payload);
    void reject(std::string reason);

    // Internal: set by the IPC layer before handing to user callback
    using ReplyFn = std::function<void(json, bool /*ok*/, std::string /*reason*/)>;
    void set_reply_fn(ReplyFn fn) { reply_fn_ = std::move(fn); }

private:
    ReplyFn reply_fn_;
};

class BinaryMessage {
public:
    std::vector<uint8_t> data;
    std::string          channel;

    void reply(std::vector<uint8_t> payload);
    void reject(std::string reason);

    using ReplyFn = std::function<void(std::vector<uint8_t>, bool, std::string)>;
    void set_reply_fn(ReplyFn fn) { reply_fn_ = std::move(fn); }

private:
    ReplyFn reply_fn_;
};

// ── IPC façade attached to WebView ───────────────────────────────────────────
// Accessed via wv.ipc — forwards to WebViewImpl.

class WebViewImpl; // forward

class IPC {
public:
    explicit IPC(WebViewImpl* impl) : impl_(impl) {}

    // JSON two-way
    void handle(const std::string& channel, std::function<void(Message&)> fn);
    void invoke(const std::string& channel, json body, std::function<void(Message&)> fn);

    // JSON fire-and-forget
    void send(const std::string& channel, json body);
    void on(const std::string& channel, std::function<void(Message&)> fn);

    // Binary two-way (overload of handle/invoke)
    void handle(const std::string& channel, std::function<void(BinaryMessage&)> fn);
    void invoke(const std::string& channel, std::vector<uint8_t> data,
                std::function<void(BinaryMessage&)> fn);

    // Binary fire-and-forget
    void send_binary(const std::string& channel, std::vector<uint8_t> data);
    void on_binary(const std::string& channel, std::function<void(BinaryMessage&)> fn);

private:
    WebViewImpl* impl_;
};

} // namespace webview