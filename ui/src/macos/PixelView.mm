#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#include "ui/PixelView.h"
#include "ui/Window.h"
#include "ui/pixel_channel.h"

#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <algorithm>
#include <vector>

// ── ObjC interface declarations ───────────────────────────────────────────────

@interface UIPixelViewObserver : NSObject
@property (nonatomic, assign) ui::PixelView::Impl* impl;
- (void)observeFrameOf:(NSView*)view;
- (void)stopObserving:(NSView*)view;
@end

@interface UIPixelNSView : NSView
@end

@interface UIPixelPoller : NSObject
@property (nonatomic, assign) ui::PixelView::Impl* impl;
- (void)startWithInterval:(double)interval_ms;
- (void)stop;
@end

// ── Impl ──────────────────────────────────────────────────────────────────────

namespace ui {

struct PixelView::Impl {
    UIPixelNSView*       view     = nil;
    UIPixelPoller*       poller   = nil;
    UIPixelViewObserver* observer = nil;

    // Config
    std::string          channel_id;
    PixelFormat          format   = PixelFormat::BGRA8;
    bool                 stretch  = true;
    std::optional<Size>  min_size;
    std::optional<Size>  max_size;

    // SHM state
    int      shm_fd       = -1;
    void*    shm_ptr      = MAP_FAILED;
    size_t   shm_map_size = 0;
    uint64_t last_frame   = UINT64_MAX;
    bool     connected    = false;
    uint64_t frame_count  = 0;

    // Callbacks
    std::function<void(Size)>        cb_resize;
    std::function<void(Point)>       cb_move;
    std::function<void()>            cb_connect;
    std::function<void()>            cb_disconnect;
    std::function<void(FrameEvent&)> cb_frame;

    void try_open();
    void close_channel(bool fire_disconnect);
    void poll();
    void render(const PixelChannelHeader* hdr, const uint8_t* data);
};

} // namespace ui

// ── UIPixelViewObserver implementation ───────────────────────────────────────

@implementation UIPixelViewObserver

- (void)observeFrameOf:(NSView*)view {
    [view addObserver:self forKeyPath:@"frame"
              options:NSKeyValueObservingOptionNew context:nil];
}

- (void)stopObserving:(NSView*)view {
    [view removeObserver:self forKeyPath:@"frame"];
}

- (void)observeValueForKeyPath:(NSString*)kp ofObject:(id)obj
                         change:(NSDictionary*)change context:(void*)ctx {
    if (![kp isEqualToString:@"frame"] || !self.impl) return;
    NSRect f = ((NSView*)obj).frame;
    ui::Size  s = { (int)f.size.width,  (int)f.size.height };
    ui::Point p = { (int)f.origin.x,    (int)f.origin.y    };
    if (self.impl->cb_resize) self.impl->cb_resize(s);
    if (self.impl->cb_move)   self.impl->cb_move(p);
}

@end

// ── UIPixelNSView implementation ──────────────────────────────────────────────

@implementation UIPixelNSView

- (instancetype)initWithFrame:(NSRect)frame {
    if ((self = [super initWithFrame:frame])) {
        self.wantsLayer = YES;
        self.layer.backgroundColor = CGColorGetConstantColor(kCGColorBlack);
    }
    return self;
}

@end

// ── UIPixelPoller implementation ──────────────────────────────────────────────

@implementation UIPixelPoller {
    NSTimer* _timer;
}

- (void)startWithInterval:(double)interval_ms {
    _timer = [NSTimer scheduledTimerWithTimeInterval:interval_ms / 1000.0
                                             target:self
                                           selector:@selector(tick:)
                                           userInfo:nil
                                            repeats:YES];
}

- (void)stop {
    [_timer invalidate];
    _timer = nil;
}

- (void)tick:(NSTimer*)t {
    if (self.impl) self.impl->poll();
}

@end

// ── Helpers ───────────────────────────────────────────────────────────────────

static NSView* pixel_parent_view(ui::Window& parent) {
    NSWindow* win = (__bridge NSWindow*)parent.native_handle().get();
    return win.contentView;
}

// ── Impl methods ──────────────────────────────────────────────────────────────

namespace ui {

void PixelView::Impl::try_open() {
    std::string name = pixel_channel_shm_name(channel_id);
    int fd = shm_open(name.c_str(), O_RDONLY, 0);
    if (fd < 0) return;

    // Peek at just the header to read data_size before committing to the full map
    void* peek = mmap(nullptr, sizeof(PixelChannelHeader), PROT_READ, MAP_SHARED, fd, 0);
    if (peek == MAP_FAILED) { ::close(fd); return; }

    auto* hdr = static_cast<const PixelChannelHeader*>(peek);
    bool valid = (hdr->magic   == kPixelChannelMagic &&
                  hdr->version == kPixelChannelVersion);
    size_t total = sizeof(PixelChannelHeader) + hdr->data_size;
    munmap(peek, sizeof(PixelChannelHeader));

    if (!valid) { ::close(fd); return; }

    void* ptr = mmap(nullptr, total, PROT_READ, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) { ::close(fd); return; }

    shm_fd       = fd;
    shm_ptr      = ptr;
    shm_map_size = total;
    last_frame   = UINT64_MAX;
    connected    = true;

    if (cb_connect) cb_connect();
}

void PixelView::Impl::close_channel(bool fire) {
    if (shm_ptr != MAP_FAILED) { munmap(shm_ptr, shm_map_size); shm_ptr = MAP_FAILED; }
    if (shm_fd  >= 0)          { ::close(shm_fd);               shm_fd  = -1; }
    shm_map_size = 0;
    last_frame   = UINT64_MAX;
    if (connected && fire && cb_disconnect) cb_disconnect();
    connected = false;
}

void PixelView::Impl::poll() {
    if (!connected) { try_open(); return; }

    auto* hdr = static_cast<const PixelChannelHeader*>(shm_ptr);

    if (hdr->magic != kPixelChannelMagic) {
        close_channel(/*fire=*/true);
        return;
    }

    uint64_t fc = hdr->frame_count;
    if (fc == last_frame) return;

    // Producer resized its buffer — remap silently
    size_t expected = sizeof(PixelChannelHeader) + hdr->data_size;
    if (expected > shm_map_size) {
        close_channel(/*fire=*/false);
        try_open();
        return;
    }

    const uint8_t* data = static_cast<const uint8_t*>(shm_ptr) + sizeof(PixelChannelHeader);
    render(hdr, data);

    last_frame = fc;
    ++frame_count;

    if (cb_frame) {
        FrameEvent ev { (int)hdr->width, (int)hdr->height,
                        (PixelFormat)hdr->format, frame_count };
        cb_frame(ev);
    }
}

void PixelView::Impl::render(const PixelChannelHeader* hdr, const uint8_t* data) {
    uint32_t w = hdr->width;
    uint32_t h = hdr->height;
    if (w == 0 || h == 0) return;

    CGColorSpaceRef cs  = CGColorSpaceCreateDeviceRGB();
    CGBitmapInfo    bmi = 0;
    size_t          bpr = 0;
    size_t          bpp = 32;
    const uint8_t*  src = data;

    std::vector<uint8_t> converted;

    switch ((PixelFormat)hdr->format) {
        case PixelFormat::BGRA8:
            bpr = w * 4;
            bmi = kCGBitmapByteOrder32Little | kCGImageAlphaPremultipliedFirst;
            break;

        case PixelFormat::RGBA8:
            bpr = w * 4;
            bmi = kCGBitmapByteOrderDefault | kCGImageAlphaPremultipliedLast;
            break;

        case PixelFormat::RGB8: {
            // Expand to 32 bpp RGBX — CGImage has poor support for 24 bpp
            converted.resize(w * h * 4);
            const uint8_t* s = data;
            uint8_t*       d = converted.data();
            for (uint32_t i = 0; i < w * h; ++i, s += 3, d += 4) {
                d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = 0xFF;
            }
            src = converted.data();
            bpr = w * 4;
            bmi = kCGBitmapByteOrderDefault | kCGImageAlphaNoneSkipLast;
            break;
        }

        case PixelFormat::YUV420: {
            // Planar I420 → BGRA (BT.601 coefficients)
            converted.resize(w * h * 4);
            const uint8_t* Y = data;
            const uint8_t* U = data + (size_t)w * h;
            const uint8_t* V = data + (size_t)w * h + ((size_t)(w / 2) * (h / 2));
            uint8_t* d = converted.data();
            for (uint32_t row = 0; row < h; ++row) {
                for (uint32_t col = 0; col < w; ++col) {
                    int y = Y[row * w + col];
                    int u = U[(row / 2) * (w / 2) + (col / 2)] - 128;
                    int v = V[(row / 2) * (w / 2) + (col / 2)] - 128;
                    int r = std::clamp(y + (int)(1.402f * v),                     0, 255);
                    int g = std::clamp(y - (int)(0.344f * u) - (int)(0.714f * v), 0, 255);
                    int b = std::clamp(y + (int)(1.772f * u),                     0, 255);
                    *d++ = (uint8_t)b;
                    *d++ = (uint8_t)g;
                    *d++ = (uint8_t)r;
                    *d++ = 0xFF;
                }
            }
            src = converted.data();
            bpr = w * 4;
            bmi = kCGBitmapByteOrder32Little | kCGImageAlphaNoneSkipFirst;
            break;
        }
    }

    // Copy into CFData so CGImage owns its memory independently of the shm region
    CFDataRef         cf       = CFDataCreate(nullptr, src, (CFIndex)(bpr * h));
    CGDataProviderRef provider = CGDataProviderCreateWithCFData(cf);
    CFRelease(cf);

    CGImageRef img = CGImageCreate(w, h, 8, bpp, bpr, cs, bmi,
                                   provider, nullptr, false, kCGRenderingIntentDefault);
    CGDataProviderRelease(provider);
    CGColorSpaceRelease(cs);

    if (img) {
        view.layer.contents = (__bridge id)img;
        CGImageRelease(img);
    }
}

} // namespace ui

// ── PixelView public API ──────────────────────────────────────────────────────

namespace ui {

PixelView::PixelView(Window& parent, PixelViewConfig config)
    : impl_(std::make_unique<Impl>())
{
    impl_->channel_id = config.channel_id;
    impl_->format     = config.format;
    impl_->stretch    = config.stretch;
    impl_->min_size   = config.min_size;
    impl_->max_size   = config.max_size;

    NSView* parent_view = pixel_parent_view(parent);
    NSRect  frame = NSMakeRect(config.position.x, config.position.y,
                               config.size.width,  config.size.height);

    UIPixelNSView* view = [[UIPixelNSView alloc] initWithFrame:frame];
    view.autoresizingMask      = NSViewNotSizable;
    view.layer.contentsGravity = config.stretch ? kCAGravityResizeAspect
                                                 : kCAGravityCenter;
    [parent_view addSubview:view];
    impl_->view = view;

    UIPixelViewObserver* obs = [[UIPixelViewObserver alloc] init];
    obs.impl = impl_.get();
    [obs observeFrameOf:view];
    impl_->observer = obs;

    UIPixelPoller* poller = [[UIPixelPoller alloc] init];
    poller.impl = impl_.get();
    [poller startWithInterval:config.poll_interval_ms];
    impl_->poller = poller;
}

PixelView::~PixelView() {
    [impl_->poller stop];
    [impl_->observer stopObserving:impl_->view];
    impl_->close_channel(/*fire=*/false);
    [impl_->view removeFromSuperview];
}

// ── Geometry ──────────────────────────────────────────────────────────────────

void PixelView::set_size(Size size) {
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

Size PixelView::get_size() const {
    NSRect f = impl_->view.frame;
    return { (int)f.size.width, (int)f.size.height };
}

void PixelView::set_position(Point point) {
    NSRect f  = impl_->view.frame;
    f.origin  = NSMakePoint(point.x, point.y);
    impl_->view.frame = f;
}

Point PixelView::get_position() const {
    NSRect f = impl_->view.frame;
    return { (int)f.origin.x, (int)f.origin.y };
}

void PixelView::set_min_size(Size size) { impl_->min_size = size; }
void PixelView::set_max_size(Size size) { impl_->max_size = size; }

// ── State ─────────────────────────────────────────────────────────────────────

void     PixelView::show()             { impl_->view.hidden = NO;  }
void     PixelView::hide()             { impl_->view.hidden = YES; }
bool     PixelView::is_visible()  const { return !impl_->view.isHidden; }
bool     PixelView::is_connected() const { return impl_->connected; }
uint64_t PixelView::get_frame_count() const { return impl_->frame_count; }

// ── Events ────────────────────────────────────────────────────────────────────

void PixelView::on_resize    (std::function<void(Size)>        fn) { impl_->cb_resize     = std::move(fn); }
void PixelView::on_move      (std::function<void(Point)>       fn) { impl_->cb_move       = std::move(fn); }
void PixelView::on_connect   (std::function<void()>            fn) { impl_->cb_connect    = std::move(fn); }
void PixelView::on_disconnect(std::function<void()>            fn) { impl_->cb_disconnect = std::move(fn); }
void PixelView::on_frame     (std::function<void(FrameEvent&)> fn) { impl_->cb_frame      = std::move(fn); }

// ── NativeHandle ──────────────────────────────────────────────────────────────

NativeHandle PixelView::native_handle() const {
    return { (__bridge void*)impl_->view, NativeHandleType::NSView };
}

} // namespace ui