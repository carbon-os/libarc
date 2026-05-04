#import "WebViewImpl.hpp"
#include <webview/webview.hpp>

namespace webview {

// ── Lifecycle setters ─────────────────────────────────────────────────────────

void WebView::on_ready(std::function<void()> fn)
    { impl_->on_ready_cb = std::move(fn); }

void WebView::on_close(std::function<bool()> fn)
    { impl_->on_close_cb = std::move(fn); }

void WebView::on_navigate(std::function<void(NavigationEvent&)> fn)
    { impl_->on_navigate_cb = std::move(fn); }

void WebView::on_title_change(std::function<void(std::string)> fn)
    { impl_->on_title_change_cb = std::move(fn); }

// ── Page load setters ─────────────────────────────────────────────────────────

void WebView::on_load_start(std::function<void(LoadEvent&)> fn)
    { impl_->on_load_start_cb = std::move(fn); }

void WebView::on_load_commit(std::function<void(LoadEvent&)> fn)
    { impl_->on_load_commit_cb = std::move(fn); }

void WebView::on_load_finish(std::function<void(LoadEvent&)> fn)
    { impl_->on_load_finish_cb = std::move(fn); }

void WebView::on_load_failed(std::function<void(LoadFailedEvent&)> fn)
    { impl_->on_load_failed_cb = std::move(fn); }

// ── Other event setters ───────────────────────────────────────────────────────

void WebView::on_new_window(std::function<void(NewWindowEvent&)> fn)
    { impl_->on_new_window_cb = std::move(fn); }

void WebView::on_auth_challenge(std::function<void(AuthChallenge&)> fn)
    { impl_->on_auth_cb = std::move(fn); }

void WebView::on_console_message(std::function<void(ConsoleMessage&)> fn)
    { impl_->on_console_cb = std::move(fn); }

void WebView::on_permission_request(std::function<void(PermissionRequest&)> fn)
    { impl_->on_permission_cb = std::move(fn); }

void WebView::on_download_start(std::function<bool(DownloadEvent&)> fn)
    { impl_->on_download_start_cb = std::move(fn); }

void WebView::on_download_progress(std::function<void(DownloadEvent&)> fn)
    { impl_->on_download_progress_cb = std::move(fn); }

void WebView::on_download_complete(std::function<void(DownloadEvent&)> fn)
    { impl_->on_download_complete_cb = std::move(fn); }

void WebView::on_request(std::function<void(ResourceRequest&)> fn)
    { impl_->on_request_cb = std::move(fn); }

} // namespace webview