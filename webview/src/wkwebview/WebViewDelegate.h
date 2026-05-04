#pragma once
#import <WebKit/WebKit.h>
#import <Cocoa/Cocoa.h>

namespace webview { class WebViewImpl; }

@interface WebViewDelegate : NSObject <
    WKNavigationDelegate,
    WKUIDelegate,
    WKScriptMessageHandler
>
@property (nonatomic) webview::WebViewImpl* impl;
@end

@interface WebViewSchemeHandler : NSObject <WKURLSchemeHandler>
@property (nonatomic, copy) NSString* htmlContent;
@property (nonatomic) webview::WebViewImpl* impl;
@end