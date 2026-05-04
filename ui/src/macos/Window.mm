#import <Cocoa/Cocoa.h>
#include "ui/Window.h"
#include <functional>

// ── Helpers ───────────────────────────────────────────────────────────────────

static NSWindowStyleMask style_mask(ui::WindowStyle style, bool resizable) {
    switch (style) {
        case ui::WindowStyle::Default: {
            NSWindowStyleMask m = NSWindowStyleMaskTitled
                                | NSWindowStyleMaskClosable
                                | NSWindowStyleMaskMiniaturizable;
            if (resizable) m |= NSWindowStyleMaskResizable;
            return m;
        }
        case ui::WindowStyle::BorderOnly:
            return NSWindowStyleMaskResizable;
        case ui::WindowStyle::Borderless:
            return NSWindowStyleMaskBorderless;
    }
}

static NSPoint to_ns_origin(ui::Point p, ui::Size window_size, NSScreen* screen) {
    CGFloat screen_h = screen ? screen.frame.size.height : NSScreen.mainScreen.frame.size.height;
    return NSMakePoint(p.x, screen_h - p.y - window_size.height);
}

static ui::Point from_ns_origin(NSPoint origin, CGFloat window_h, NSScreen* screen) {
    CGFloat screen_h = screen ? screen.frame.size.height : NSScreen.mainScreen.frame.size.height;
    return { (int)origin.x, (int)(screen_h - origin.y - window_h) };
}

// ── ObjC delegate ─────────────────────────────────────────────────────────────

@interface UIWindowDelegate : NSObject <NSWindowDelegate>
@property (nonatomic, assign) ui::Window::Impl* impl;
@end

// ── Drop-accepting content view ───────────────────────────────────────────────

@interface UIDropContentView : NSView
@property (nonatomic, assign) ui::Window::Impl* impl;
@end

// ── Impl definition ───────────────────────────────────────────────────────────

namespace ui {

struct Window::Impl {
    NSWindow*              window       = nil;
    UIWindowDelegate*      delegate     = nil;
    UIDropContentView*     content_view = nil;
    NSVisualEffectView*    effect_view  = nil;

    std::function<void(Size)>        cb_resize;
    std::function<void(Point)>       cb_move;
    std::function<void()>            cb_focus;
    std::function<void()>            cb_blur;
    std::function<void(WindowState)> cb_state_change;
    std::function<bool()>            cb_close;
    std::function<void(DropEvent&)>  cb_drop;
};

} // namespace ui

// ── UIWindowDelegate implementation ──────────────────────────────────────────

@implementation UIWindowDelegate

- (void)windowDidResize:(NSNotification*)n {
    if (!self.impl->cb_resize) return;
    // Use contentView bounds — this matches window.innerWidth/Height in JS
    // and excludes the native title bar height on macOS.
    NSSize s = ((NSWindow*)n.object).contentView.bounds.size;
    self.impl->cb_resize({ (int)s.width, (int)s.height });
}

- (void)windowDidMove:(NSNotification*)n {
    if (!self.impl->cb_move) return;
    NSWindow* w = n.object;
    NSRect f = w.frame;
    self.impl->cb_move(from_ns_origin(f.origin, f.size.height, w.screen));
}

- (void)windowDidBecomeKey:(NSNotification*)n {
    if (self.impl->cb_focus) self.impl->cb_focus();
}

- (void)windowDidResignKey:(NSNotification*)n {
    if (self.impl->cb_blur) self.impl->cb_blur();
}

- (void)windowDidMiniaturize:(NSNotification*)n {
    if (self.impl->cb_state_change)
        self.impl->cb_state_change(ui::WindowState::Minimized);
}

- (void)windowDidDeminiaturize:(NSNotification*)n {
    if (self.impl->cb_state_change)
        self.impl->cb_state_change(ui::WindowState::Normal);
}

- (void)windowDidEnterFullScreen:(NSNotification*)n {
    if (self.impl->cb_state_change)
        self.impl->cb_state_change(ui::WindowState::Fullscreen);
}

- (void)windowDidExitFullScreen:(NSNotification*)n {
    if (self.impl->cb_state_change)
        self.impl->cb_state_change(ui::WindowState::Normal);
}

- (void)windowDidZoom:(NSNotification*)n {
    NSWindow* w = n.object;
    if (self.impl->cb_state_change)
        self.impl->cb_state_change(w.isZoomed ? ui::WindowState::Maximized
                                               : ui::WindowState::Normal);
}

- (BOOL)windowShouldClose:(NSWindow*)sender {
    BOOL should_close = self.impl->cb_close ? (self.impl->cb_close() ? YES : NO) : YES;
    if (should_close) [NSApp terminate:nil];
    return should_close;
}

@end

// ── UIDropContentView implementation ─────────────────────────────────────────

@implementation UIDropContentView

- (void)awakeFromNib { [super awakeFromNib]; [self setup]; }
- (instancetype)initWithFrame:(NSRect)f {
    if ((self = [super initWithFrame:f])) [self setup];
    return self;
}
- (void)setup {
    [self registerForDraggedTypes:@[NSPasteboardTypeFileURL]];
}

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
    NSPasteboard* pb = sender.draggingPasteboard;
    if ([pb canReadObjectForClasses:@[NSURL.class]
                            options:@{NSPasteboardURLReadingFileURLsOnlyKey: @YES}])
        return NSDragOperationCopy;
    return NSDragOperationNone;
}

- (NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)sender {
    return [self draggingEntered:sender];
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    if (!self.impl || !self.impl->cb_drop) return NO;
    NSArray<NSURL*>* urls = [sender.draggingPasteboard
        readObjectsForClasses:@[NSURL.class]
                      options:@{NSPasteboardURLReadingFileURLsOnlyKey: @YES}];
    if (!urls.count) return NO;

    ui::DropEvent ev;
    for (NSURL* u in urls)
        ev.paths.push_back(u.path.UTF8String);

    NSPoint loc = sender.draggingLocation;
    ev.position = { (int)loc.x, (int)(self.bounds.size.height - loc.y) };
    self.impl->cb_drop(ev);
    return YES;
}

@end

// ── Backdrop effect helper ────────────────────────────────────────────────────

static void apply_effect(NSWindow* window,
                         NSVisualEffectView* __strong* effect_view_ptr,
                         ui::BackdropEffect effect) {
    if (*effect_view_ptr) {
        [*effect_view_ptr removeFromSuperview];
        *effect_view_ptr = nil;
    }

    if (effect != ui::BackdropEffect::Vibrancy) return;

    NSView* content = window.contentView;
    NSVisualEffectView* ev = [[NSVisualEffectView alloc] initWithFrame:content.bounds];
    ev.material         = NSVisualEffectMaterialSidebar;
    ev.blendingMode     = NSVisualEffectBlendingModeBehindWindow;
    ev.state            = NSVisualEffectStateActive;
    ev.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [content addSubview:ev positioned:NSWindowBelow relativeTo:nil];
    *effect_view_ptr = ev;
}

// ── Window implementation ─────────────────────────────────────────────────────

namespace ui {

Window::Window(WindowConfig config)
    : impl_(std::make_unique<Impl>())
{
    NSWindowStyleMask mask = style_mask(config.style, config.resizable);

    NSRect content_rect = NSMakeRect(0, 0, config.size.width, config.size.height);
    NSWindow* win = [[NSWindow alloc]
        initWithContentRect:content_rect
                  styleMask:mask
                    backing:NSBackingStoreBuffered
                      defer:NO];

    win.title = [NSString stringWithUTF8String:config.title.c_str()];

    if (config.transparent) {
        win.backgroundColor = NSColor.clearColor;
        win.opaque = NO;
    }

    if (config.always_on_top)
        win.level = NSFloatingWindowLevel;

    UIDropContentView* cv =
        [[UIDropContentView alloc] initWithFrame:win.contentView.bounds];
    cv.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    win.contentView = cv;
    impl_->content_view = cv;

    if (config.min_size)
        win.contentMinSize = NSMakeSize(config.min_size->width, config.min_size->height);
    if (config.max_size)
        win.contentMaxSize = NSMakeSize(config.max_size->width, config.max_size->height);

    if (config.position) {
        NSPoint origin = to_ns_origin(*config.position, config.size, NSScreen.mainScreen);
        [win setFrameOrigin:origin];
    } else {
        [win center];
    }

    if (config.effect)
        apply_effect(win, &impl_->effect_view, *config.effect);

    UIWindowDelegate* del = [[UIWindowDelegate alloc] init];
    del.impl = impl_.get();
    win.delegate = del;
    impl_->delegate = del;
    impl_->window   = win;

    [win makeKeyAndOrderFront:nil];
}

Window::~Window() {
    impl_->window.delegate = nil;
    impl_->delegate        = nil;
    [impl_->window close];
}

// ── Geometry ──────────────────────────────────────────────────────────────────

void Window::set_size(Size size) {
    [impl_->window setContentSize:NSMakeSize(size.width, size.height)];
}

Size Window::get_size() const {
    // Return content-area size to stay consistent with resize events.
    NSSize s = impl_->window.contentView.bounds.size;
    return { (int)s.width, (int)s.height };
}

void Window::set_position(Point point) {
    NSRect f = impl_->window.frame;
    NSPoint origin = to_ns_origin(point, { (int)f.size.width, (int)f.size.height },
                                  impl_->window.screen);
    [impl_->window setFrameOrigin:origin];
}

Point Window::get_position() const {
    NSRect f = impl_->window.frame;
    return from_ns_origin(f.origin, f.size.height, impl_->window.screen);
}

void Window::center() { [impl_->window center]; }

void Window::set_min_size(Size size) {
    impl_->window.contentMinSize = NSMakeSize(size.width, size.height);
}

void Window::set_max_size(Size size) {
    impl_->window.contentMaxSize = NSMakeSize(size.width, size.height);
}

// ── State ─────────────────────────────────────────────────────────────────────

void Window::show()     { [impl_->window makeKeyAndOrderFront:nil]; }
void Window::hide()     { [impl_->window orderOut:nil]; }
void Window::focus()    { [impl_->window makeKeyAndOrderFront:nil]; }
void Window::minimize() { [impl_->window miniaturize:nil]; }
void Window::restore()  { [impl_->window deminiaturize:nil]; }

void Window::maximize() {
    NSScreen* screen = impl_->window.screen ?: NSScreen.mainScreen;
    [impl_->window setFrame:screen.visibleFrame display:YES animate:YES];
}

void Window::run() {
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp activateIgnoringOtherApps:YES];
    [NSApp run];
}

void Window::set_fullscreen(bool fullscreen) {
    if (fullscreen != is_fullscreen())
        [impl_->window toggleFullScreen:nil];
}

bool Window::is_fullscreen() const {
    return (impl_->window.styleMask & NSWindowStyleMaskFullScreen) != 0;
}

bool Window::is_visible() const { return impl_->window.isVisible; }
bool Window::is_focused() const { return impl_->window.isKeyWindow; }

WindowState Window::get_state() const {
    if (is_fullscreen())              return WindowState::Fullscreen;
    if (impl_->window.isMiniaturized) return WindowState::Minimized;
    if (impl_->window.isZoomed)       return WindowState::Maximized;
    return WindowState::Normal;
}

// ── Appearance ────────────────────────────────────────────────────────────────

void Window::set_title(std::string title) {
    impl_->window.title = [NSString stringWithUTF8String:title.c_str()];
}

std::string Window::get_title() const {
    return impl_->window.title.UTF8String;
}

void Window::set_always_on_top(bool on_top) {
    impl_->window.level = on_top ? NSFloatingWindowLevel : NSNormalWindowLevel;
}

void Window::set_effect(BackdropEffect effect) {
    apply_effect(impl_->window, &impl_->effect_view, effect);
}

void Window::clear_effect() {
    if (impl_->effect_view) {
        [impl_->effect_view removeFromSuperview];
        impl_->effect_view = nil;
    }
}

// ── Events ────────────────────────────────────────────────────────────────────

void Window::on_resize      (std::function<void(Size)>        fn) { impl_->cb_resize       = std::move(fn); }
void Window::on_move        (std::function<void(Point)>       fn) { impl_->cb_move         = std::move(fn); }
void Window::on_focus       (std::function<void()>            fn) { impl_->cb_focus        = std::move(fn); }
void Window::on_blur        (std::function<void()>            fn) { impl_->cb_blur         = std::move(fn); }
void Window::on_state_change(std::function<void(WindowState)> fn) { impl_->cb_state_change = std::move(fn); }
void Window::on_close       (std::function<bool()>            fn) { impl_->cb_close        = std::move(fn); }
void Window::on_drop        (std::function<void(DropEvent&)>  fn) {
    impl_->cb_drop = std::move(fn);
    impl_->content_view.impl = impl_.get();
}

// ── NativeHandle ──────────────────────────────────────────────────────────────

NativeHandle Window::native_handle() const {
    return { (__bridge void*)impl_->window, NativeHandleType::NSWindow };
}

} // namespace ui