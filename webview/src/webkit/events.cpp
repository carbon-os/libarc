#include "WebViewImpl.hpp"

namespace webview {

void WebViewImpl::on_load_changed(WebKitWebView* web_view, WebKitLoadEvent load_event, gpointer user_data) {
    auto* self = static_cast<WebViewImpl*>(user_data);
    const gchar* uri = webkit_web_view_get_uri(web_view);
    std::string url_str = uri ? uri : "";

    switch (load_event) {
        case WEBKIT_LOAD_STARTED:
            if (self->on_load_start_cb) {
                LoadEvent ev{ url_str, true };
                self->on_load_start_cb(ev);
            }
            break;
        case WEBKIT_LOAD_COMMITTED:
            if (self->on_load_commit_cb) {
                LoadEvent ev{ url_str, true };
                self->on_load_commit_cb(ev);
            }
            break;
        case WEBKIT_LOAD_FINISHED:
            if (!self->ready_) {
                self->ready_ = true;
                if (self->on_ready_cb) self->on_ready_cb();
            }
            if (self->on_load_finish_cb) {
                LoadEvent ev{ url_str, true };
                self->on_load_finish_cb(ev);
            }
            break;
        default:
            break;
    }
}

void WebViewImpl::on_title_changed(GObject* object, GParamSpec*, gpointer user_data) {
    auto* self = static_cast<WebViewImpl*>(user_data);
    if (self->on_title_change_cb) {
        const gchar* title = webkit_web_view_get_title(WEBKIT_WEB_VIEW(object));
        self->on_title_change_cb(title ? title : "");
    }
}

gboolean WebViewImpl::on_close_requested(WebKitWebView*, gpointer user_data) {
    auto* self = static_cast<WebViewImpl*>(user_data);
    if (self->on_close_cb) {
        bool allow_close = self->on_close_cb();
        return allow_close ? FALSE : TRUE; 
    }
    return FALSE;
}

gboolean WebViewImpl::on_decide_policy(WebKitWebView* web_view, WebKitPolicyDecision* decision, WebKitPolicyDecisionType type, gpointer user_data) {
    auto* self = static_cast<WebViewImpl*>(user_data);

    if (type == WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION) {
        WebKitNavigationPolicyDecision* nav_decision = WEBKIT_NAVIGATION_POLICY_DECISION(decision);
        WebKitNavigationAction* action = webkit_navigation_policy_decision_get_navigation_action(nav_decision);
        WebKitURIRequest* request = webkit_navigation_action_get_request(action);
        
        if (self->on_navigate_cb) {
            NavigationEvent ev;
            ev.url = webkit_uri_request_get_uri(request);
            self->on_navigate_cb(ev);
            
            if (ev.is_cancelled()) {
                webkit_policy_decision_ignore(decision);
                return TRUE;
            }
        }
    } else if (type == WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION) {
        WebKitNavigationPolicyDecision* nav_decision = WEBKIT_NAVIGATION_POLICY_DECISION(decision);
        WebKitNavigationAction* action = webkit_navigation_policy_decision_get_navigation_action(nav_decision);
        WebKitURIRequest* request = webkit_navigation_action_get_request(action);

        if (self->on_new_window_cb) {
            NewWindowEvent ev;
            ev.url = webkit_uri_request_get_uri(request);
            ev.is_user_gesture = webkit_navigation_action_is_user_gesture(action);
            self->on_new_window_cb(ev);

            if (ev.is_cancelled() || !ev.redirect_url().empty()) {
                webkit_policy_decision_ignore(decision);
                if (!ev.redirect_url().empty()) {
                    webkit_web_view_load_uri(web_view, ev.redirect_url().c_str());
                }
                return TRUE;
            }
        }
    }
    return FALSE;
}

void WebViewImpl::on_script_message_received(WebKitUserContentManager*, WebKitJavascriptResult* js_result, gpointer user_data) {
    auto* self = static_cast<WebViewImpl*>(user_data);
    if (!self->on_console_cb) return;

    JSCValue* value = webkit_javascript_result_get_js_value(js_result);
    if (jsc_value_is_object(value)) {
        char* json_str = jsc_value_to_json(value, 0);
        if (json_str) {
            json dict = json::parse(json_str, nullptr, false);
            g_free(json_str);

            ConsoleMessage cm;
            cm.text = dict.value("text", "");
            std::string level = dict.value("level", "log");
            
            if (level == "info") cm.level = ConsoleLevel::Info;
            else if (level == "warn") cm.level = ConsoleLevel::Warn;
            else if (level == "error") cm.level = ConsoleLevel::Error;
            else cm.level = ConsoleLevel::Log;

            self->on_console_cb(cm);
        }
    }
    webkit_javascript_result_unref(js_result);
}

void WebViewImpl::on_download_started(WebKitWebContext*, WebKitDownload* download, gpointer user_data) {
    auto* self = static_cast<WebViewImpl*>(user_data);
    std::string did = std::to_string(reinterpret_cast<uintptr_t>(download));
    
    DownloadEvent ev;
    ev.id = did;
    WebKitURIRequest* req = webkit_download_get_request(download);
    ev.url = webkit_uri_request_get_uri(req);
    
    WebKitURIResponse* resp = webkit_download_get_response(download);
    if (resp) {
        ev.total_bytes = webkit_uri_response_get_content_length(resp);
        const gchar* suggested = webkit_uri_response_get_suggested_filename(resp);
        if (suggested) ev.suggested_filename = suggested;
    }

    bool allow = true;
    if (self->on_download_start_cb) {
        allow = self->on_download_start_cb(ev);
    }

    if (!allow || ev.is_cancelled()) {
        webkit_download_cancel(download);
        return;
    }

    self->active_downloads_[did] = ev;

    if (!ev.destination.empty()) {
        std::string dest_uri = "file://" + ev.destination;
        webkit_download_set_destination(download, dest_uri.c_str());
    }

    g_signal_connect(download, "received-data", G_CALLBACK(+[](WebKitDownload* d, guint64 len, gpointer ud) {
        auto* impl = static_cast<WebViewImpl*>(ud);
        std::string id = std::to_string(reinterpret_cast<uintptr_t>(d));
        if (impl->active_downloads_.count(id) && impl->on_download_progress_cb) {
            impl->active_downloads_[id].bytes_received += len;
            impl->on_download_progress_cb(impl->active_downloads_[id]);
        }
    }), self);

    g_signal_connect(download, "finished", G_CALLBACK(+[](WebKitDownload* d, gpointer ud) {
        auto* impl = static_cast<WebViewImpl*>(ud);
        std::string id = std::to_string(reinterpret_cast<uintptr_t>(d));
        if (impl->active_downloads_.count(id)) {
            impl->active_downloads_[id].set_failed(false);
            if (impl->on_download_complete_cb) impl->on_download_complete_cb(impl->active_downloads_[id]);
            impl->active_downloads_.erase(id);
        }
    }), self);
    
    g_signal_connect(download, "failed", G_CALLBACK(+[](WebKitDownload* d, GError*, gpointer ud) {
        auto* impl = static_cast<WebViewImpl*>(ud);
        std::string id = std::to_string(reinterpret_cast<uintptr_t>(d));
        if (impl->active_downloads_.count(id)) {
            impl->active_downloads_[id].set_failed(true);
            if (impl->on_download_complete_cb) impl->on_download_complete_cb(impl->active_downloads_[id]);
            impl->active_downloads_.erase(id);
        }
    }), self);
}

gboolean WebViewImpl::on_authenticate(WebKitWebView*, WebKitAuthenticationRequest* request, gpointer user_data) {
    auto* self = static_cast<WebViewImpl*>(user_data);
    if (!self->on_auth_cb) return FALSE;

    AuthChallenge ac;
    ac.host = webkit_authentication_request_get_host(request);
    ac.realm = webkit_authentication_request_get_realm(request);
    ac.is_proxy = webkit_authentication_request_is_for_proxy(request);

    self->on_auth_cb(ac);

    if (ac.action() == AuthChallenge::Action::Respond) {
        WebKitCredential* new_cred = webkit_credential_new(ac.user().c_str(), ac.password().c_str(), WEBKIT_CREDENTIAL_PERSISTENCE_FOR_SESSION);
        webkit_authentication_request_authenticate(request, new_cred);
        webkit_credential_free(new_cred);
        return TRUE;
    } else if (ac.action() == AuthChallenge::Action::Cancel) {
        webkit_authentication_request_cancel(request);
        return TRUE;
    }
    
    return FALSE;
}

// ── Lifecycle setters ─────────────────────────────────────────────────────────

void WebView::on_ready(std::function<void()> fn) { impl_->on_ready_cb = std::move(fn); }
void WebView::on_close(std::function<bool()> fn) { impl_->on_close_cb = std::move(fn); }
void WebView::on_navigate(std::function<void(NavigationEvent&)> fn) { impl_->on_navigate_cb = std::move(fn); }
void WebView::on_title_change(std::function<void(std::string)> fn) { impl_->on_title_change_cb = std::move(fn); }
void WebView::on_load_start(std::function<void(LoadEvent&)> fn) { impl_->on_load_start_cb = std::move(fn); }
void WebView::on_load_commit(std::function<void(LoadEvent&)> fn) { impl_->on_load_commit_cb = std::move(fn); }
void WebView::on_load_finish(std::function<void(LoadEvent&)> fn) { impl_->on_load_finish_cb = std::move(fn); }
void WebView::on_load_failed(std::function<void(LoadFailedEvent&)> fn) { impl_->on_load_failed_cb = std::move(fn); }
void WebView::on_download_start(std::function<bool(DownloadEvent&)> fn) { impl_->on_download_start_cb = std::move(fn); }
void WebView::on_download_progress(std::function<void(DownloadEvent&)> fn) { impl_->on_download_progress_cb = std::move(fn); }
void WebView::on_download_complete(std::function<void(DownloadEvent&)> fn) { impl_->on_download_complete_cb = std::move(fn); }
void WebView::on_new_window(std::function<void(NewWindowEvent&)> fn) { impl_->on_new_window_cb = std::move(fn); }
void WebView::on_auth_challenge(std::function<void(AuthChallenge&)> fn) { impl_->on_auth_cb = std::move(fn); }
void WebView::on_console_message(std::function<void(ConsoleMessage&)> fn) { impl_->on_console_cb = std::move(fn); }

} // namespace webview