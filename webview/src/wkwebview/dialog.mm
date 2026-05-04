#import "WebViewImpl.hpp"
#include <webview/webview.hpp>
#include <optional>
#include <string>
#include <vector>

#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>   // UTType full definition

namespace webview {

static NSArray<UTType*>* build_content_types(const std::vector<FileFilter>& filters)
    API_AVAILABLE(macos(11.0)) {
    NSMutableArray* types = [NSMutableArray array];
    for (auto& f : filters) {
        for (auto& ext : f.extensions) {
            UTType* t = [UTType typeWithFilenameExtension:@(ext.c_str())];
            if (t) [types addObject:t];
        }
    }
    return types.count ? types : nil;
}

static NSArray<NSString*>* build_extensions(const std::vector<FileFilter>& filters) {
    NSMutableArray* exts = [NSMutableArray array];
    for (auto& f : filters)
        for (auto& e : f.extensions)
            [exts addObject:@(e.c_str())];
    return exts.count ? exts : nil;
}

// Suppress deprecation warnings for the macOS < 11 fallback path only.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

static void apply_filters(NSSavePanel* panel, const std::vector<FileFilter>& filters) {
    if (@available(macOS 11.0, *)) {
        NSArray* types = build_content_types(filters);
        if (types) panel.allowedContentTypes = types;
    } else {
        panel.allowedFileTypes = build_extensions(filters);
    }
}

#pragma clang diagnostic pop

std::optional<std::string> WebViewImpl::show_dialog(const FileDialog& d) {
    if (d.mode == FileDialog::Mode::Save) {
        NSSavePanel* panel = [NSSavePanel savePanel];
        if (!d.title.empty())        panel.title = @(d.title.c_str());
        if (!d.default_path.empty()) panel.directoryURL =
            [NSURL fileURLWithPath:@(d.default_path.c_str())];

        apply_filters(panel, d.filters);

        if ([panel runModal] == NSModalResponseOK)
            return panel.URL.path.UTF8String ?: "";
        return std::nullopt;
    }

    NSOpenPanel* panel = [NSOpenPanel openPanel];
    if (!d.title.empty())        panel.title = @(d.title.c_str());
    if (!d.default_path.empty()) panel.directoryURL =
        [NSURL fileURLWithPath:@(d.default_path.c_str())];

    panel.allowsMultipleSelection = NO;
    panel.canChooseFiles          = YES;
    panel.canChooseDirectories    = NO;

    apply_filters(panel, d.filters);

    if ([panel runModal] == NSModalResponseOK)
        return panel.URL.path.UTF8String ?: "";
    return std::nullopt;
}

std::vector<std::string> WebViewImpl::show_dialog_multi(const FileDialog& d) {
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    if (!d.title.empty())        panel.title = @(d.title.c_str());
    if (!d.default_path.empty()) panel.directoryURL =
        [NSURL fileURLWithPath:@(d.default_path.c_str())];

    panel.allowsMultipleSelection = YES;
    panel.canChooseFiles          = YES;
    panel.canChooseDirectories    = NO;

    apply_filters(panel, d.filters);

    std::vector<std::string> result;
    if ([panel runModal] == NSModalResponseOK) {
        for (NSURL* url in panel.URLs)
            result.push_back(url.path.UTF8String ?: "");
    }
    return result;
}

// ── Public WebView forwarders ─────────────────────────────────────────────────

std::optional<std::string> WebView::dialog(FileDialog d)
    { return impl_->show_dialog(d); }

std::vector<std::string> WebView::dialog_multi(FileDialog d)
    { return impl_->show_dialog_multi(d); }

} // namespace webview