#import "WebViewImpl.hpp"
#include <webview/webview.hpp>
#include <string>

@interface WebViewInterceptSchemeHandler : NSObject <WKURLSchemeHandler>
@property (nonatomic) webview::WebViewImpl* impl;
@property (nonatomic, copy) NSString* htmlContent;
@end

@implementation WebViewInterceptSchemeHandler

- (void)webView:(WKWebView*)webView
    startURLSchemeTask:(id<WKURLSchemeTask>)task {

    auto* impl = self.impl;

    __block webview::ResourceRequest req;   // __block so the block can write into it
    req.url    = task.request.URL.absoluteString.UTF8String ?: "";
    req.method = task.request.HTTPMethod.UTF8String ?: "GET";

    [task.request.allHTTPHeaderFields enumerateKeysAndObjectsUsingBlock:
        ^(NSString* k, NSString* v, BOOL*) {
            req.headers[k.UTF8String] = v.UTF8String;
        }];

    NSString* accept = task.request.allHTTPHeaderFields[@"Accept"] ?: @"";
    if ([accept containsString:@"text/html"])        req.resource_type = webview::ResourceType::Document;
    else if ([accept containsString:@"javascript"])  req.resource_type = webview::ResourceType::Script;
    else if ([accept containsString:@"image"])       req.resource_type = webview::ResourceType::Image;
    else                                             req.resource_type = webview::ResourceType::Other;

    if (impl && impl->on_request_cb) {
        impl->on_request_cb(req);
    }

    switch (req.action()) {
        case webview::ResourceRequest::Action::Cancel:
            [task didFailWithError:
                [NSError errorWithDomain:NSURLErrorDomain
                                   code:NSURLErrorCancelled userInfo:nil]];
            return;

        case webview::ResourceRequest::Action::Redirect: {
            NSURL* dest = [NSURL URLWithString:@(req.redirect_url().c_str())];
            NSURLRequest* r = [NSURLRequest requestWithURL:dest];
            NSURLResponse* resp = [[NSURLResponse alloc] initWithURL:dest
                                                            MIMEType:@"text/html"
                                               expectedContentLength:-1
                                                    textEncodingName:nil];
            [task didReceiveResponse:resp];
            [task didFinish];
            return;
        }

        case webview::ResourceRequest::Action::Respond: {
            auto& res = req.response();
            NSMutableDictionary* hdrs = [NSMutableDictionary dictionary];
            for (auto& [k, v] : res.headers)
                hdrs[@(k.c_str())] = @(v.c_str());

            NSHTTPURLResponse* resp =
                [[NSHTTPURLResponse alloc] initWithURL:task.request.URL
                                            statusCode:res.status
                                           HTTPVersion:@"HTTP/1.1"
                                          headerFields:hdrs];
            NSData* body = [NSData dataWithBytes:res.body.data()
                                          length:res.body.size()];
            [task didReceiveResponse:resp];
            [task didReceiveData:body];
            [task didFinish];
            return;
        }

        default:
            break;
    }

    NSString* html = self.htmlContent ?: @"";
    NSData*   data = [html dataUsingEncoding:NSUTF8StringEncoding];
    NSURLResponse* resp = [[NSURLResponse alloc] initWithURL:task.request.URL
                                                    MIMEType:@"text/html"
                                       expectedContentLength:(NSInteger)data.length
                                            textEncodingName:@"utf-8"];
    [task didReceiveResponse:resp];
    [task didReceiveData:data];
    [task didFinish];
}

- (void)webView:(WKWebView*)webView
    stopURLSchemeTask:(id<WKURLSchemeTask>)task {}

@end

// ─────────────────────────────────────────────────────────────────────────────

namespace webview {

void WebView::on_request(std::function<void(ResourceRequest&)> fn) {
    impl_->on_request_cb = std::move(fn);

    if (![impl_->wk_config_ urlSchemeHandlerForURLScheme:@"webview"]) {
        NSLog(@"[webview] on_request: webview:// scheme handler already finalised. "
              @"Register on_request() before the first navigation for it to take effect.");
    }
}

} // namespace webview