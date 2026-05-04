#import "WebViewImpl.hpp"
#include <webview/webview.hpp>
#include <string>

namespace webview {

void WebViewImpl::eval(const std::string& js, std::function<void(EvalResult)> fn) {
    [wkwebview_ evaluateJavaScript:@(js.c_str())
                 completionHandler:^(id result, NSError* error) {
        EvalResult r;
        if (error) {
            r.error = error.localizedDescription.UTF8String ?: "unknown error";
        } else if (result) {
            // WKWebView returns NSString, NSNumber, NSDictionary, NSArray, or nil
            if ([result isKindOfClass:[NSString class]]) {
                r.value = json(((NSString*)result).UTF8String);
            } else if ([result isKindOfClass:[NSNumber class]]) {
                // Distinguish bool from number
                if (strcmp(((NSNumber*)result).objCType, @encode(BOOL)) == 0) {
                    r.value = (bool)((NSNumber*)result).boolValue;
                } else {
                    r.value = ((NSNumber*)result).doubleValue;
                }
            } else if ([result isKindOfClass:[NSDictionary class]]
                    || [result isKindOfClass:[NSArray class]]) {
                NSData*   data    = [NSJSONSerialization dataWithJSONObject:result
                                                                   options:0 error:nil];
                NSString* jsonStr = [[NSString alloc] initWithData:data
                                                          encoding:NSUTF8StringEncoding];
                r.value = json::parse(jsonStr.UTF8String, nullptr, false);
            }
            // NSNull → json null (default constructed)
        }
        if (fn) fn(std::move(r));
    }];
}

void WebViewImpl::add_user_script(const std::string& js, ScriptInjectTime time) {
    WKUserScriptInjectionTime wkTime =
        (time == ScriptInjectTime::DocumentStart)
            ? WKUserScriptInjectionTimeAtDocumentStart
            : WKUserScriptInjectionTimeAtDocumentEnd;

    WKUserScript* script =
        [[WKUserScript alloc] initWithSource:@(js.c_str())
                               injectionTime:wkTime
                            forMainFrameOnly:NO];
    [wk_config_.userContentController addUserScript:script];
}

void WebViewImpl::remove_user_scripts() {
    [wk_config_.userContentController removeAllUserScripts];
}

// ── Public WebView forwarders ─────────────────────────────────────────────────

void WebView::eval(std::string js, std::function<void(EvalResult)> fn)
    { impl_->eval(js, std::move(fn)); }

void WebView::add_user_script(std::string js, ScriptInjectTime time)
    { impl_->add_user_script(js, time); }

void WebView::remove_user_scripts()
    { impl_->remove_user_scripts(); }

} // namespace webview