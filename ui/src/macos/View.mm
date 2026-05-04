#import <Cocoa/Cocoa.h>
#include "ui/View.h"
#include "ui/Window.h"

// ── ObjC resize-tracking view ─────────────────────────────────────────────────

@interface UIViewObserver : NSObject
@property (nonatomic, assign) ui::View::Impl* impl;
- (void)observeFrameOf:(NSView*)view;
- (void)stopObserving:(NSView*)view;
@end

// ── Impl ─────────────────────────────────────────────────────────────────────

namespace ui {

struct View::Impl {
    NSView*             view        = nil;
    NSVisualEffectView* effect_view = nil;
    UIViewObserver*     observer    = nil;

    std::optional<Size>  min_size;
    std::optional<Size>  max_size;

    std::function<void(Size)>  cb_resize;
    std::function<void(Point)> cb_move;
    std::function<void()>      cb_focus;
    std::function<void()>      cb_blur;
};

} // namespace ui

// ── UIViewObserver implementation ─────────────────────────────────────────────

@implementation UIViewObserver

- (void)observeFrameOf:(NSView*)view {
    [view addObserver:self
           forKeyPath:@"frame"
              options:NSKeyValueObservingOptionNew
              context:nil];
}

- (void)stopObserving:(NSView*)view {
    [view removeObserver:self forKeyPath:@"frame"];
}

- (void)observeValueForKeyPath:(NSString*)kp
                       ofObject:(id)obj
                         change:(NSDictionary*)change
                        context:(void*)ctx
{
    if (![kp isEqualToString:@"frame"] || !self.impl) return;
    NSRect f = ((NSView*)obj).frame;
    ui::Size  s = { (int)f.size.width,  (int)f.size.height };
    ui::Point p = { (int)f.origin.x,    (int)f.origin.y    };
    if (self.impl->cb_resize) self.impl->cb_resize(s);
    if (self.impl->cb_move)   self.impl->cb_move(p);
}

@end

// ── Backdrop helper ───────────────────────────────────────────────────────────

static void apply_view_effect(NSView* view,
                               NSVisualEffectView* __strong* evp,
                               ui::BackdropEffect effect)
{
    if (*evp) { [*evp removeFromSuperview]; *evp = nil; }
    if (effect != ui::BackdropEffect::Vibrancy) return;

    NSVisualEffectView* ev = [[NSVisualEffectView alloc] initWithFrame:view.bounds];
    ev.material         = NSVisualEffectMaterialSidebar;
    ev.blendingMode     = NSVisualEffectBlendingModeWithinWindow;
    ev.state            = NSVisualEffectStateActive;
    ev.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [view addSubview:ev positioned:NSWindowBelow relativeTo:nil];
    *evp = ev;
}

// ── NativeHandle extraction ───────────────────────────────────────────────────

static NSView* content_view_from_window(ui::Window& parent) {
    NSWindow* win = (__bridge NSWindow*)parent.native_handle().get();
    return win.contentView;
}

// ── View implementation ───────────────────────────────────────────────────────

namespace ui {

View::View(Window& parent, ViewConfig config)
    : impl_(std::make_unique<Impl>())
{
    NSView* parent_view = content_view_from_window(parent);

    NSRect frame = NSMakeRect(config.position.x, config.position.y,
                              config.size.width,  config.size.height);
    NSView* view = [[NSView alloc] initWithFrame:frame];
    view.autoresizingMask = NSViewNotSizable;

    if (config.transparent)
        view.layer.backgroundColor = CGColorGetConstantColor(kCGColorClear);

    impl_->min_size = config.min_size;
    impl_->max_size = config.max_size;

    if (config.effect)
        apply_view_effect(view, &impl_->effect_view, *config.effect);

    [parent_view addSubview:view];
    impl_->view = view;

    UIViewObserver* obs = [[UIViewObserver alloc] init];
    obs.impl = impl_.get();
    [obs observeFrameOf:view];
    impl_->observer = obs;
}

View::~View() {
    [impl_->observer stopObserving:impl_->view];
    [impl_->view removeFromSuperview];
}

// ── Geometry ──────────────────────────────────────────────────────────────────

void View::set_size(Size size) {
    if (impl_->min_size) {
        size.width  = std::max(size.width,  impl_->min_size->width);
        size.height = std::max(size.height, impl_->min_size->height);
    }
    if (impl_->max_size) {
        size.width  = std::min(size.width,  impl_->max_size->width);
        size.height = std::min(size.height, impl_->max_size->height);
    }
    NSRect f  = impl_->view.frame;
    f.size    = NSMakeSize(size.width, size.height);
    impl_->view.frame = f;
}

Size View::get_size() const {
    NSRect f = impl_->view.frame;
    return { (int)f.size.width, (int)f.size.height };
}

void View::set_position(Point point) {
    NSRect f  = impl_->view.frame;
    f.origin  = NSMakePoint(point.x, point.y);
    impl_->view.frame = f;
}

Point View::get_position() const {
    NSRect f = impl_->view.frame;
    return { (int)f.origin.x, (int)f.origin.y };
}

void View::set_min_size(Size size) { impl_->min_size = size; }
void View::set_max_size(Size size) { impl_->max_size = size; }

// ── State ─────────────────────────────────────────────────────────────────────

void View::show()  { impl_->view.hidden = NO;  }
void View::hide()  { impl_->view.hidden = YES; }
void View::focus() { [impl_->view.window makeFirstResponder:impl_->view]; }

bool View::is_visible() const { return !impl_->view.isHidden; }
bool View::is_focused()  const {
    return impl_->view.window.firstResponder == impl_->view;
}

// ── Stacking ──────────────────────────────────────────────────────────────────

void View::bring_to_front() {
    NSView* parent = impl_->view.superview;
    if (!parent) return;
    // Re-inserting above nil = above all siblings (topmost).
    [parent addSubview:impl_->view positioned:NSWindowAbove relativeTo:nil];
}

void View::send_to_back() {
    NSView* parent = impl_->view.superview;
    if (!parent) return;
    // Re-inserting below nil = below all siblings (bottommost).
    [parent addSubview:impl_->view positioned:NSWindowBelow relativeTo:nil];
}

// ── Appearance ────────────────────────────────────────────────────────────────

void View::set_effect(BackdropEffect effect) {
    apply_view_effect(impl_->view, &impl_->effect_view, effect);
}

void View::clear_effect() {
    if (impl_->effect_view) {
        [impl_->effect_view removeFromSuperview];
        impl_->effect_view = nil;
    }
}

// ── Events ────────────────────────────────────────────────────────────────────

void View::on_resize(std::function<void(Size)>  fn) { impl_->cb_resize = std::move(fn); }
void View::on_move  (std::function<void(Point)> fn) { impl_->cb_move   = std::move(fn); }
void View::on_focus (std::function<void()>      fn) { impl_->cb_focus  = std::move(fn); }
void View::on_blur  (std::function<void()>      fn) { impl_->cb_blur   = std::move(fn); }

// ── NativeHandle ──────────────────────────────────────────────────────────────

NativeHandle View::native_handle() const {
    return { (__bridge void*)impl_->view, NativeHandleType::NSView };
}

} // namespace ui