#pragma once
#include <string>

namespace webview {

// ── Navigation ────────────────────────────────────────────────────────────────

class NavigationEvent {
public:
    std::string url;

    void cancel()        { cancelled_ = true; }
    bool is_cancelled()  const { return cancelled_; }

private:
    bool cancelled_ = false;
};

// ── Page load ─────────────────────────────────────────────────────────────────

struct LoadEvent {
    std::string url;
    bool        is_main_frame = true;
};

struct LoadFailedEvent : LoadEvent {
    int         error_code = 0;
    std::string error_description;
};

// ── Console ───────────────────────────────────────────────────────────────────

enum class ConsoleLevel { Log, Info, Warn, Error };

struct ConsoleMessage {
    ConsoleLevel level      = ConsoleLevel::Log;
    std::string  text;
    std::string  source_url;   // best-effort on WKWebView (injected shim)
    int          line       = 0;
};

// ── New window ────────────────────────────────────────────────────────────────

class NewWindowEvent {
public:
    std::string url;
    bool        is_user_gesture = false;

    void cancel()                     { cancelled_ = true; }
    void redirect(std::string target) { redirect_url_ = std::move(target); }

    bool               is_cancelled()   const { return cancelled_; }
    const std::string& redirect_url()   const { return redirect_url_; }

private:
    bool        cancelled_ = false;
    std::string redirect_url_;
};

} // namespace webview