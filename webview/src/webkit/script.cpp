#include "WebViewImpl.hpp"

namespace webview {

void WebViewImpl::eval_js_raw(const std::string& js) {
    webkit_web_view_evaluate_javascript(webkit_webview_, js.c_str(), -1, nullptr, nullptr, nullptr, nullptr, nullptr);
}

void WebViewImpl::eval(const std::string& js, std::function<void(EvalResult)> fn) {
    auto callback = [](GObject* source_object, GAsyncResult* res, gpointer user_data) {
        auto* func = static_cast<std::function<void(EvalResult)>*>(user_data);
        GError* error = nullptr;
        
        // In WebKit2GTK 4.1+, this returns a JSCValue* directly
        JSCValue* value = webkit_web_view_evaluate_javascript_finish(WEBKIT_WEB_VIEW(source_object), res, &error);

        EvalResult result;
        if (error) {
            result.error = error->message;
            g_error_free(error);
        } else if (value) {
            // We already have the 'value', we just process it normally
            if (jsc_value_is_string(value)) {
                char* str = jsc_value_to_string(value);
                result.value = json(str);
                g_free(str);
            } else if (jsc_value_is_number(value)) {
                result.value = jsc_value_to_double(value);
            } else if (jsc_value_is_boolean(value)) {
                result.value = jsc_value_to_boolean(value);
            } else if (jsc_value_is_object(value) || jsc_value_is_array(value)) {
                char* json_str = jsc_value_to_json(value, 0);
                if (json_str) {
                    result.value = json::parse(json_str, nullptr, false);
                    g_free(json_str);
                }
            }
            // Release the JSCValue using GObject unref
            g_object_unref(value);
        }

        if (*func) (*func)(std::move(result));
        delete func;
    };

    auto* func_ptr = new std::function<void(EvalResult)>(std::move(fn));
    webkit_web_view_evaluate_javascript(webkit_webview_, js.c_str(), -1, nullptr, nullptr, nullptr, callback, func_ptr);
}

void WebViewImpl::add_user_script(const std::string& js, ScriptInjectTime time) {
    WebKitUserScriptInjectionTime inj_time = (time == ScriptInjectTime::DocumentStart)
        ? WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START
        : WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_END;

    WebKitUserScript* script = webkit_user_script_new(
        js.c_str(), WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES, inj_time, nullptr, nullptr);
    webkit_user_content_manager_add_script(ucm_, script);
    webkit_user_script_unref(script);
}

void WebViewImpl::remove_user_scripts() {
    webkit_user_content_manager_remove_all_scripts(ucm_);
}

// ── Public WebView forwarders ─────────────────────────────────────────────────

void WebView::eval(std::string js, std::function<void(EvalResult)> fn)
    { impl_->eval(js, std::move(fn)); }

void WebView::add_user_script(std::string js, ScriptInjectTime time)
    { impl_->add_user_script(js, time); }

void WebView::remove_user_scripts()
    { impl_->remove_user_scripts(); }

} // namespace webview