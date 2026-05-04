#import "WebViewImpl.hpp"
#include <webview/webview.hpp>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// WKDownloadDelegate — macOS 11.3+
// ─────────────────────────────────────────────────────────────────────────────

API_AVAILABLE(macos(11.3))
@interface WebViewDownloadDelegate : NSObject <WKDownloadDelegate>
@property (nonatomic) webview::WebViewImpl* impl;
@end

@implementation WebViewDownloadDelegate

- (void)download:(WKDownload*)download
    decideDestinationUsingResponse:(NSURLResponse*)response
                 suggestedFilename:(NSString*)suggestedFilename
                 completionHandler:(void (^)(NSURL* _Nullable))handler
    API_AVAILABLE(macos(11.3)) {

    auto* impl = self.impl;

    std::string did = [NSString stringWithFormat:@"%p", (void*)download].UTF8String;

    webview::DownloadEvent ev;
    ev.id                 = did;
    ev.url                = download.originalRequest.URL.absoluteString.UTF8String ?: "";
    ev.suggested_filename = suggestedFilename.UTF8String ?: "";
    ev.total_bytes        = response.expectedContentLength;

    bool allow = true;
    if (impl && impl->on_download_start_cb) {
        allow = impl->on_download_start_cb(ev);
    }

    if (!allow || ev.is_cancelled()) {
        // cancel: requires a completion handler (block), nil is valid
        [download cancel:nil];
        handler(nil);
        return;
    }

    if (impl) impl->active_downloads_[did] = ev;

    NSURL* dest = nil;
    if (!ev.destination.empty()) {
        dest = [NSURL fileURLWithPath:@(ev.destination.c_str())];
    } else {
        NSURL* downloads = [[NSFileManager defaultManager]
            URLForDirectory:NSDownloadsDirectory
                   inDomain:NSUserDomainMask
          appropriateForURL:nil
                     create:YES error:nil];
        dest = [downloads URLByAppendingPathComponent:suggestedFilename];
    }
    handler(dest);
}

- (void)downloadDidFinish:(WKDownload*)download API_AVAILABLE(macos(11.3)) {
    auto* impl = self.impl;
    std::string did = [NSString stringWithFormat:@"%p", (void*)download].UTF8String;
    auto it = impl ? impl->active_downloads_.find(did) : impl->active_downloads_.end();
    if (it != impl->active_downloads_.end()) {
        it->second.set_failed(false);
        if (impl->on_download_complete_cb) impl->on_download_complete_cb(it->second);
        impl->active_downloads_.erase(it);
    }
}

- (void)download:(WKDownload*)download
    didFailWithError:(NSError*)error
          resumeData:(NSData*)resumeData
    API_AVAILABLE(macos(11.3)) {

    auto* impl = self.impl;
    std::string did = [NSString stringWithFormat:@"%p", (void*)download].UTF8String;
    auto it = impl ? impl->active_downloads_.find(did) : impl->active_downloads_.end();
    if (it != impl->active_downloads_.end()) {
        it->second.set_failed(true);
        if (impl->on_download_complete_cb) impl->on_download_complete_cb(it->second);
        impl->active_downloads_.erase(it);
    }
}

@end

// ─────────────────────────────────────────────────────────────────────────────

namespace webview {

void WebView::on_permission_request(std::function<void(PermissionRequest&)> fn)
    { impl_->on_permission_cb = std::move(fn); }

} // namespace webview