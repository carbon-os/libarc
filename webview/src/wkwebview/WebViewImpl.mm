#import "WebViewDelegate.h"
#import "WebViewImpl.hpp"

#include <webview/webview.hpp>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// WebViewSchemeHandler — serves all webview:// traffic.
//
// URL routing by host:
//   webview://ipc/*  →  IPC (handle_scheme_ipc_task)
//   webview://app/*  →  static files from resource_root, or inline htmlContent
//   anything else    →  404
// ─────────────────────────────────────────────────────────────────────────────

// Private helpers declared in a category so the main @implementation stays clean.
@interface WebViewSchemeHandler (Private)
- (NSString*)mimeTypeForPath:(NSString*)path;
- (void)replyToTask:(id<WKURLSchemeTask>)task
         statusCode:(int)code
               data:(NSData*)data
           mimeType:(NSString*)mime
            headers:(NSDictionary*)extraHeaders;
- (void)replyToTask:(id<WKURLSchemeTask>)task
         statusCode:(int)code
            message:(NSString*)msg;
@end

@implementation WebViewSchemeHandler (Private)

- (NSString*)mimeTypeForPath:(NSString*)path {
    static NSDictionary* map = nil;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        map = @{
            @"html":  @"text/html;charset=utf-8",
            @"htm":   @"text/html;charset=utf-8",
            @"js":    @"application/javascript",
            @"mjs":   @"application/javascript",
            @"css":   @"text/css",
            @"json":  @"application/json",
            @"wasm":  @"application/wasm",
            @"png":   @"image/png",
            @"jpg":   @"image/jpeg",
            @"jpeg":  @"image/jpeg",
            @"gif":   @"image/gif",
            @"webp":  @"image/webp",
            @"svg":   @"image/svg+xml",
            @"ico":   @"image/x-icon",
            @"ttf":   @"font/ttf",
            @"woff":  @"font/woff",
            @"woff2": @"font/woff2",
            @"txt":   @"text/plain;charset=utf-8",
            @"xml":   @"application/xml",
            @"mp4":   @"video/mp4",
            @"webm":  @"video/webm",
        };
    });
    return map[path.pathExtension.lowercaseString] ?: @"application/octet-stream";
}

- (void)replyToTask:(id<WKURLSchemeTask>)task
         statusCode:(int)code
               data:(NSData*)data
           mimeType:(NSString*)mime
            headers:(NSDictionary*)extraHeaders {
    NSMutableDictionary* hdrs = [@{
        @"Content-Type":                mime ?: @"application/octet-stream",
        @"Access-Control-Allow-Origin": @"*",
        @"Cache-Control":               @"no-store",
    } mutableCopy];
    if (extraHeaders) [hdrs addEntriesFromDictionary:extraHeaders];

    NSHTTPURLResponse* resp =
        [[NSHTTPURLResponse alloc] initWithURL:task.request.URL
                                    statusCode:code
                                   HTTPVersion:@"HTTP/1.1"
                                  headerFields:hdrs];
    @try {
        [task didReceiveResponse:resp];
        if (data.length) [task didReceiveData:data];
        [task didFinish];
    } @catch (NSException*) {}
}

- (void)replyToTask:(id<WKURLSchemeTask>)task
         statusCode:(int)code
            message:(NSString*)msg {
    NSData* d = [msg dataUsingEncoding:NSUTF8StringEncoding];
    [self replyToTask:task statusCode:code data:d
             mimeType:@"text/plain;charset=utf-8" headers:nil];
}

@end

// ─────────────────────────────────────────────────────────────────────────────

@implementation WebViewSchemeHandler

- (void)webView:(WKWebView*)webView
    startURLSchemeTask:(id<WKURLSchemeTask>)task {

    NSURL*    url  = task.request.URL;
    NSString* host = url.host ?: @"";

    // ── webview://ipc/* ───────────────────────────────────────────────────────
    if ([host isEqualToString:@"ipc"]) {
        if (self.impl)
            self.impl->handle_scheme_ipc_task((__bridge void*)task);
        else
            [self replyToTask:task statusCode:500 message:@"IPC not ready"];
        return;
    }

    // ── Unknown host ──────────────────────────────────────────────────────────
    if (![host isEqualToString:@"app"]) {
        [self replyToTask:task statusCode:404 message:@"Unknown host"];
        return;
    }

    // ── webview://app/* ───────────────────────────────────────────────────────

    auto* impl = self.impl;
    if (impl && impl->on_request_cb) {
        __block webview::ResourceRequest req;
        req.url    = url.absoluteString.UTF8String ?: "";
        req.method = task.request.HTTPMethod.UTF8String ?: "GET";

        [task.request.allHTTPHeaderFields enumerateKeysAndObjectsUsingBlock:
            ^(NSString* k, NSString* v, BOOL*) {
                req.headers[k.UTF8String] = v.UTF8String;
            }];

        NSString* accept = task.request.allHTTPHeaderFields[@"Accept"] ?: @"";
        if      ([accept containsString:@"text/html"])      req.resource_type = webview::ResourceType::Document;
        else if ([accept containsString:@"javascript"])     req.resource_type = webview::ResourceType::Script;
        else if ([accept containsString:@"image"])          req.resource_type = webview::ResourceType::Image;
        else                                                req.resource_type = webview::ResourceType::Other;

        impl->on_request_cb(req);

        switch (req.action()) {
            case webview::ResourceRequest::Action::Cancel:
                [task didFailWithError:
                    [NSError errorWithDomain:NSURLErrorDomain
                                       code:NSURLErrorCancelled userInfo:nil]];
                return;

            case webview::ResourceRequest::Action::Redirect: {
                NSURL* dest = [NSURL URLWithString:@(req.redirect_url().c_str())];
                NSURLResponse* r = [[NSURLResponse alloc]
                    initWithURL:dest MIMEType:@"text/html"
                    expectedContentLength:-1 textEncodingName:nil];
                @try { [task didReceiveResponse:r]; [task didFinish]; }
                @catch (NSException*) {}
                return;
            }

            case webview::ResourceRequest::Action::Respond: {
                auto& res = req.response();
                NSMutableDictionary* hdrs = [NSMutableDictionary dictionary];
                for (auto& [k, v] : res.headers)
                    hdrs[@(k.c_str())] = @(v.c_str());
                NSData* body = [NSData dataWithBytes:res.body.data()
                                             length:res.body.size()];
                NSHTTPURLResponse* r = [[NSHTTPURLResponse alloc]
                    initWithURL:url statusCode:res.status
                    HTTPVersion:@"HTTP/1.1" headerFields:hdrs];
                @try {
                    [task didReceiveResponse:r];
                    if (body.length) [task didReceiveData:body];
                    [task didFinish];
                } @catch (NSException*) {}
                return;
            }

            default: break;
        }
    }

    // ── Serve from resource_root ──────────────────────────────────────────────
    std::string root = impl ? impl->resource_root_ : "";

    if (!root.empty()) {
        NSString* urlPath = url.path;
        if (!urlPath.length || [urlPath isEqualToString:@"/"])
            urlPath = @"/index.html";

        NSString* joined   = [NSString stringWithFormat:@"%s%@", root.c_str(), urlPath];
        NSString* resolved = joined.stringByResolvingSymlinksInPath;
        NSString* rootRes  = [@(root.c_str()) stringByResolvingSymlinksInPath];

        if (![resolved hasPrefix:rootRes]) {
            [self replyToTask:task statusCode:403 message:@"Forbidden"];
            return;
        }

        NSData* data = [NSData dataWithContentsOfFile:resolved];
        if (!data) {
            [self replyToTask:task statusCode:404 message:@"Not found"];
            return;
        }

        [self replyToTask:task statusCode:200 data:data
                 mimeType:[self mimeTypeForPath:resolved] headers:nil];
        return;
    }

    // ── Fallback: inline htmlContent ──────────────────────────────────────────
    NSString* html = self.htmlContent ?: @"";
    NSData*   data = [html dataUsingEncoding:NSUTF8StringEncoding];
    NSURLResponse* r = [[NSURLResponse alloc]
        initWithURL:url MIMEType:@"text/html"
        expectedContentLength:(NSInteger)data.length
        textEncodingName:@"utf-8"];
    @try {
        [task didReceiveResponse:r];
        if (data.length) [task didReceiveData:data];
        [task didFinish];
    } @catch (NSException*) {}
}

- (void)webView:(WKWebView*)webView
    stopURLSchemeTask:(id<WKURLSchemeTask>)task {
    if (self.impl)
        self.impl->on_scheme_task_stopped((__bridge void*)task);
}

@end

// ─────────────────────────────────────────────────────────────────────────────
// WebViewDelegate
// ─────────────────────────────────────────────────────────────────────────────

@implementation WebViewDelegate

// ── WKNavigationDelegate ──────────────────────────────────────────────────────

- (void)webView:(WKWebView*)webView
    decidePolicyForNavigationAction:(WKNavigationAction*)action
                    decisionHandler:(void (^)(WKNavigationActionPolicy))handler {

    auto* impl = self.impl;
    if (!impl || !impl->on_navigate_cb) { handler(WKNavigationActionPolicyAllow); return; }

    webview::NavigationEvent ev;
    ev.url = action.request.URL.absoluteString.UTF8String ?: "";
    impl->on_navigate_cb(ev);
    handler(ev.is_cancelled() ? WKNavigationActionPolicyCancel
                               : WKNavigationActionPolicyAllow);
}

- (void)webView:(WKWebView*)webView
    didStartProvisionalNavigation:(WKNavigation*)navigation {

    auto* impl = self.impl;
    if (!impl || !impl->on_load_start_cb) return;

    webview::LoadEvent ev;
    ev.url           = webView.URL.absoluteString.UTF8String ?: "";
    ev.is_main_frame = true;
    impl->on_load_start_cb(ev);
}

- (void)webView:(WKWebView*)webView
    didCommitNavigation:(WKNavigation*)navigation {

    auto* impl = self.impl;
    if (!impl || !impl->on_load_commit_cb) return;

    webview::LoadEvent ev;
    ev.url           = webView.URL.absoluteString.UTF8String ?: "";
    ev.is_main_frame = true;
    impl->on_load_commit_cb(ev);
}

- (void)webView:(WKWebView*)webView
    didFinishNavigation:(WKNavigation*)navigation {

    auto* impl = self.impl;
    if (!impl) return;

    if (!impl->ready_) {
        impl->ready_ = true;
        if (impl->on_ready_cb) impl->on_ready_cb();
    }

    if (impl->on_load_finish_cb) {
        webview::LoadEvent ev;
        ev.url           = webView.URL.absoluteString.UTF8String ?: "";
        ev.is_main_frame = true;
        impl->on_load_finish_cb(ev);
    }
}

- (void)webView:(WKWebView*)webView
    didFailNavigation:(WKNavigation*)navigation
            withError:(NSError*)error {

    auto* impl = self.impl;
    if (!impl || !impl->on_load_failed_cb) return;

    webview::LoadFailedEvent ev;
    ev.url               = webView.URL.absoluteString.UTF8String ?: "";
    ev.is_main_frame     = true;
    ev.error_code        = (int)error.code;
    ev.error_description = error.localizedDescription.UTF8String ?: "";
    impl->on_load_failed_cb(ev);
}

- (void)webView:(WKWebView*)webView
    didFailProvisionalNavigation:(WKNavigation*)navigation
                       withError:(NSError*)error {
    [self webView:webView didFailNavigation:navigation withError:error];
}

- (void)webView:(WKWebView*)webView
    didReceiveAuthenticationChallenge:(NSURLAuthenticationChallenge*)challenge
                    completionHandler:(void (^)(NSURLSessionAuthChallengeDisposition,
                                               NSURLCredential*))handler {
    auto* impl = self.impl;
    if (!impl || !impl->on_auth_cb) {
        handler(NSURLSessionAuthChallengePerformDefaultHandling, nil);
        return;
    }

    webview::AuthChallenge ac;
    ac.host     = challenge.protectionSpace.host.UTF8String ?: "";
    ac.realm    = challenge.protectionSpace.realm.UTF8String ?: "";
    ac.is_proxy = (challenge.protectionSpace.proxyType != nil);
    impl->on_auth_cb(ac);

    switch (ac.action()) {
        case webview::AuthChallenge::Action::Respond: {
            NSURLCredential* cred =
                [NSURLCredential credentialWithUser:@(ac.user().c_str())
                                           password:@(ac.password().c_str())
                                        persistence:NSURLCredentialPersistenceNone];
            handler(NSURLSessionAuthChallengeUseCredential, cred);
            break;
        }
        case webview::AuthChallenge::Action::Cancel:
            handler(NSURLSessionAuthChallengeCancelAuthenticationChallenge, nil);
            break;
        default:
            handler(NSURLSessionAuthChallengePerformDefaultHandling, nil);
    }
}

- (void)webView:(WKWebView*)webView
    decidePolicyForNavigationResponse:(WKNavigationResponse*)response
                      decisionHandler:(void (^)(WKNavigationResponsePolicy))handler {
    handler(WKNavigationResponsePolicyAllow);
}

// ── WKUIDelegate ──────────────────────────────────────────────────────────────

- (void)webViewDidClose:(WKWebView*)webView {
    auto* impl = self.impl;
    if (!impl || !impl->on_close_cb) return;
    impl->on_close_cb();
}

- (WKWebView*)webView:(WKWebView*)webView
    createWebViewWithConfiguration:(WKWebViewConfiguration*)config
               forNavigationAction:(WKNavigationAction*)action
                    windowFeatures:(WKWindowFeatures*)features {

    auto* impl = self.impl;
    if (!impl) return nil;

    webview::NewWindowEvent ev;
    ev.url             = action.request.URL.absoluteString.UTF8String ?: "";
    ev.is_user_gesture = (action.navigationType == WKNavigationTypeLinkActivated);

    if (impl->on_new_window_cb) {
        impl->on_new_window_cb(ev);
        if (!ev.redirect_url().empty()) {
            [webView loadRequest:[NSURLRequest requestWithURL:
                [NSURL URLWithString:@(ev.redirect_url().c_str())]]];
        }
    }
    return nil;
}

- (void)webView:(WKWebView*)webView
    requestMediaCapturePermissionForOrigin:(WKSecurityOrigin*)origin
                          initiatedByFrame:(WKFrameInfo*)frame
                                      type:(WKMediaCaptureType)type
                           decisionHandler:(void (^)(WKPermissionDecision))handler
    API_AVAILABLE(macos(12.0)) {

    auto* impl = self.impl;
    if (!impl || !impl->on_permission_cb) { handler(WKPermissionDecisionDeny); return; }

    webview::PermissionRequest req;
    req.origin = origin.host.UTF8String ?: "";
    switch (type) {
        case WKMediaCaptureTypeCamera:
        case WKMediaCaptureTypeCameraAndMicrophone:
            req.permission = webview::PermissionType::Camera;     break;
        case WKMediaCaptureTypeMicrophone:
            req.permission = webview::PermissionType::Microphone; break;
        default:
            handler(WKPermissionDecisionDeny); return;
    }
    impl->on_permission_cb(req);
    handler(req.is_granted() ? WKPermissionDecisionGrant : WKPermissionDecisionDeny);
}

- (void)webView:(WKWebView*)webView
    runJavaScriptAlertPanelWithMessage:(NSString*)message
                      initiatedByFrame:(WKFrameInfo*)frame
                     completionHandler:(void (^)(void))handler {
    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = message;
    [alert runModal];
    handler();
}

- (void)observeValueForKeyPath:(NSString*)key
                      ofObject:(id)object
                        change:(NSDictionary*)change
                       context:(void*)context {
    if ([key isEqualToString:@"title"]) {
        auto* impl = self.impl;
        if (impl && impl->on_title_change_cb) {
            WKWebView* wv = (WKWebView*)object;
            std::string title = wv.title.UTF8String ?: "";
            impl->on_title_change_cb(title);
        }
    }
}

// ── WKScriptMessageHandler ────────────────────────────────────────────────────

- (void)userContentController:(WKUserContentController*)controller
      didReceiveScriptMessage:(WKScriptMessage*)message {

    auto* impl = self.impl;
    if (!impl) return;
    if (![message.name isEqualToString:@"__wv_console__"]) return;
    if (![message.body isKindOfClass:[NSDictionary class]])  return;

    NSDictionary* dict  = message.body;
    NSString*     level = dict[@"level"];
    NSString*     text  = dict[@"text"];

    if (!impl->on_console_cb) return;

    webview::ConsoleMessage cm;
    cm.text = text.UTF8String ?: "";
    if      ([level isEqualToString:@"info"])  cm.level = webview::ConsoleLevel::Info;
    else if ([level isEqualToString:@"warn"])  cm.level = webview::ConsoleLevel::Warn;
    else if ([level isEqualToString:@"error"]) cm.level = webview::ConsoleLevel::Error;
    else                                       cm.level = webview::ConsoleLevel::Log;

    impl->on_console_cb(cm);
}

@end

// ─────────────────────────────────────────────────────────────────────────────
// WebViewImpl — construction / teardown / setup
// ─────────────────────────────────────────────────────────────────────────────

namespace webview {

WebViewImpl::WebViewImpl(NativeHandle handle, WebViewConfig config) {
    devtools_enabled_ = config.devtools;
    resource_root_    = config.resource_root;
    setup(handle, config);
}

WebViewImpl::~WebViewImpl() {
    if (wkwebview_) {
        [wkwebview_ removeObserver:delegate_ forKeyPath:@"title"];
        [wkwebview_ removeFromSuperview];
        [wkwebview_.configuration.userContentController
            removeAllScriptMessageHandlers];
    }
}

void WebViewImpl::setup(NativeHandle handle, WebViewConfig config) {
    wk_config_ = [[WKWebViewConfiguration alloc] init];

    scheme_handler_      = [[WebViewSchemeHandler alloc] init];
    scheme_handler_.impl = this;
    [wk_config_ setURLSchemeHandler:scheme_handler_ forURLScheme:@"webview"];

    if (@available(macOS 11.0, *))
        wk_config_.defaultWebpagePreferences.allowsContentJavaScript = YES;

    if (config.devtools)
        [wk_config_.preferences setValue:@YES forKey:@"developerExtrasEnabled"];

    WKUserContentController* ucc = [[WKUserContentController alloc] init];
    wk_config_.userContentController = ucc;

    delegate_      = [[WebViewDelegate alloc] init];
    delegate_.impl = this;

    [ucc addScriptMessageHandler:delegate_ name:@"__wv_console__"];

    install_ipc_bridge();

    // ── Resolve parent view ───────────────────────────────────────────────────
    NSView* parentView = nil;
    switch (handle.type()) {
        case NativeHandleType::NSWindow:
            parentView = ((__bridge NSWindow*)handle.get()).contentView;
            break;
        case NativeHandleType::NSView:
            parentView = (__bridge NSView*)handle.get();
            break;
        default:
            NSLog(@"[webview] Unsupported NativeHandleType on macOS");
            return;
    }

    // ── Create WKWebView ──────────────────────────────────────────────────────
    wkwebview_ = [[WKWebView alloc] initWithFrame:parentView.bounds
                                    configuration:wk_config_];
    wkwebview_.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;

    wkwebview_.navigationDelegate = delegate_;
    wkwebview_.UIDelegate         = delegate_;

    if (config.devtools) {
        if (@available(macOS 13.3, *))
            wkwebview_.inspectable = YES;
    }

    [wkwebview_ addObserver:delegate_
                 forKeyPath:@"title"
                    options:NSKeyValueObservingOptionNew
                    context:nil];

    held_invoke_tasks_     = [NSMutableDictionary dictionary];
    held_bin_invoke_tasks_ = [NSMutableDictionary dictionary];

    [parentView addSubview:wkwebview_];

    auto* impl_ptr = this;
    dispatch_async(dispatch_get_main_queue(), ^{
        if (!impl_ptr->ready_) {
            impl_ptr->ready_ = true;
            if (impl_ptr->on_ready_cb) impl_ptr->on_ready_cb();
        }
    });
}

void WebViewImpl::set_resource_root(const std::string& path) {
    resource_root_ = path;
}

void WebViewImpl::eval_js_raw(const std::string& js) {
    [wkwebview_ evaluateJavaScript:@(js.c_str()) completionHandler:nil];
}

// ── Public WebView constructor / destructor ───────────────────────────────────

WebView::WebView(NativeHandle handle, WebViewConfig config)
    : impl_(std::make_unique<WebViewImpl>(handle, config))
    , ipc(impl_.get())
{}

WebView::~WebView() = default;

void WebView::set_resource_root(std::string path) {
    impl_->set_resource_root(path);
}

} // namespace webview