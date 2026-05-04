#include <gtk/gtk.h>
#include "ui/PixelView.h"
#include "ui/Window.h"
#include "ui/pixel_channel.h"

#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <algorithm>
#include <vector>

// ── Impl ──────────────────────────────────────────────────────────────────────
namespace ui {

struct PixelView::Impl {
    GtkWidget* widget = nullptr;

    // Config
    std::string         channel_id;
    PixelFormat         format    = PixelFormat::BGRA8;
    bool                stretch   = true;
    std::optional<Size> min_size;
    std::optional<Size> max_size;

    // SHM state
    int      shm_fd       = -1;
    void*    shm_ptr      = MAP_FAILED;
    size_t   shm_map_size = 0;
    uint64_t last_frame   = UINT64_MAX;
    bool     connected    = false;
    uint64_t frame_count  = 0;
    
    guint timer_id        = 0;

    // Frame Buffer Cache for Cairo
    std::vector<uint8_t> frame_buf;
    uint32_t             frame_w = 0;
    uint32_t             frame_h = 0;

    // Callbacks
    std::function<void(Size)>        cb_resize;
    std::function<void(Point)>       cb_move;
    std::function<void()>            cb_connect;
    std::function<void()>            cb_disconnect;
    std::function<void(FrameEvent&)> cb_frame;

    void try_open();
    void close_channel(bool fire_disconnect);
    void poll();
    void process_frame(const PixelChannelHeader* hdr, const uint8_t* data);

    static gboolean on_draw(GtkWidget* widget, cairo_t* cr, gpointer data);
    static gboolean on_timer(gpointer data);
};

} // namespace ui

namespace ui {

gboolean PixelView::Impl::on_timer(gpointer data) {
    auto* impl = static_cast<Impl*>(data);
    impl->poll();
    return TRUE; // Continue polling
}

gboolean PixelView::Impl::on_draw(GtkWidget* widget, cairo_t* cr, gpointer data) {
    auto* impl = static_cast<Impl*>(data);

    if (impl->frame_buf.empty() || impl->frame_w == 0 || impl->frame_h == 0) {
        cairo_set_source_rgb(cr, 0, 0, 0); // Black background
        cairo_paint(cr);
        return FALSE;
    }

    GtkAllocation alloc;
    gtk_widget_get_allocation(widget, &alloc);
    
    // Create Cairo surface from BGRA buffer
    int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, impl->frame_w);
    cairo_surface_t* surface = cairo_image_surface_create_for_data(
        impl->frame_buf.data(), CAIRO_FORMAT_ARGB32, impl->frame_w, impl->frame_h, stride);

    if (impl->stretch) {
        double scale_x = (double)alloc.width / impl->frame_w;
        double scale_y = (double)alloc.height / impl->frame_h;
        double scale = std::min(scale_x, scale_y);

        double dx = (alloc.width - (impl->frame_w * scale)) / 2.0;
        double dy = (alloc.height - (impl->frame_h * scale)) / 2.0;

        cairo_set_source_rgb(cr, 0, 0, 0);
        cairo_paint(cr);

        cairo_translate(cr, dx, dy);
        cairo_scale(cr, scale, scale);
    } else {
        double dx = (alloc.width - impl->frame_w) / 2.0;
        double dy = (alloc.height - impl->frame_h) / 2.0;
        
        cairo_set_source_rgb(cr, 0, 0, 0);
        cairo_paint(cr);
        
        cairo_translate(cr, dx, dy);
    }

    cairo_set_source_surface(cr, surface, 0, 0);
    cairo_paint(cr);
    cairo_surface_destroy(surface);

    return FALSE;
}

// ── Shared Memory & Polling ───────────────────────────────────────────────────

void PixelView::Impl::try_open() {
    std::string name = pixel_channel_shm_name(channel_id);
    int fd = shm_open(name.c_str(), O_RDONLY, 0);
    if (fd < 0) return;

    void* peek = mmap(nullptr, sizeof(PixelChannelHeader), PROT_READ, MAP_SHARED, fd, 0);
    if (peek == MAP_FAILED) { ::close(fd); return; }

    auto* hdr = static_cast<const PixelChannelHeader*>(peek);
    bool valid = (hdr->magic == kPixelChannelMagic && hdr->version == kPixelChannelVersion);
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
    if (shm_fd >= 0)           { ::close(shm_fd); shm_fd = -1; }
    shm_map_size = 0;
    last_frame   = UINT64_MAX;
    if (connected && fire && cb_disconnect) cb_disconnect();
    connected = false;
}

void PixelView::Impl::poll() {
    if (!connected) { try_open(); return; }

    auto* hdr = static_cast<const PixelChannelHeader*>(shm_ptr);
    if (hdr->magic != kPixelChannelMagic) {
        close_channel(true);
        return;
    }

    uint64_t fc = hdr->frame_count;
    if (fc == last_frame) return;

    size_t expected = sizeof(PixelChannelHeader) + hdr->data_size;
    if (expected > shm_map_size) {
        close_channel(false);
        try_open();
        return;
    }

    const uint8_t* data = static_cast<const uint8_t*>(shm_ptr) + sizeof(PixelChannelHeader);
    process_frame(hdr, data);

    last_frame = fc;
    ++frame_count;

    if (cb_frame) {
        FrameEvent ev {(int)hdr->width, (int)hdr->height, (PixelFormat)hdr->format, frame_count};
        cb_frame(ev);
    }
}

void PixelView::Impl::process_frame(const PixelChannelHeader* hdr, const uint8_t* data) {
    uint32_t w = hdr->width;
    uint32_t h = hdr->height;
    if (w == 0 || h == 0) return;

    // Convert everything to CAIRO_FORMAT_ARGB32 (BGRA in memory on little-endian)
    frame_buf.resize(w * h * 4);
    
    switch ((PixelFormat)hdr->format) {
        case PixelFormat::BGRA8:
            std::copy(data, data + (w * h * 4), frame_buf.begin());
            break;

        case PixelFormat::RGBA8: {
            const uint8_t* s = data;
            uint8_t* d = frame_buf.data();
            for (uint32_t i = 0; i < w * h; ++i, s += 4, d += 4) {
                d[0] = s[2]; // B
                d[1] = s[1]; // G
                d[2] = s[0]; // R
                d[3] = s[3]; // A
            }
            break;
        }

        case PixelFormat::RGB8: {
            const uint8_t* s = data;
            uint8_t* d = frame_buf.data();
            for (uint32_t i = 0; i < w * h; ++i, s += 3, d += 4) {
                d[0] = s[2];
                d[1] = s[1];
                d[2] = s[0];
                d[3] = 0xFF;
            }
            break;
        }

        case PixelFormat::YUV420: {
            const uint8_t* Y = data;
            const uint8_t* U = data + (size_t)w * h;
            const uint8_t* V = data + (size_t)w * h + (size_t)(w / 2) * (h / 2);
            uint8_t* d = frame_buf.data();
            for (uint32_t row = 0; row < h; ++row) {
                for (uint32_t col = 0; col < w; ++col) {
                    int y = Y[row * w + col];
                    int u = U[(row / 2) * (w / 2) + (col / 2)] - 128;
                    int v = V[(row / 2) * (w / 2) + (col / 2)] - 128;
                    int r = std::clamp(y + (int)(1.402f * v), 0, 255);
                    int g = std::clamp(y - (int)(0.344f * u) - (int)(0.714f * v), 0, 255);
                    int b = std::clamp(y + (int)(1.772f * u), 0, 255);
                    *d++ = (uint8_t)b;
                    *d++ = (uint8_t)g;
                    *d++ = (uint8_t)r;
                    *d++ = 0xFF;
                }
            }
            break;
        }
    }

    frame_w = w;
    frame_h = h;

    gtk_widget_queue_draw(widget);
}

// ── Constructor / Destructor ──────────────────────────────────────────────────

PixelView::PixelView(Window& parent, PixelViewConfig config) : impl_(std::make_unique<Impl>()) {
    impl_->channel_id = config.channel_id;
    impl_->format     = config.format;
    impl_->stretch    = config.stretch;
    impl_->min_size   = config.min_size;
    impl_->max_size   = config.max_size;

    impl_->widget = gtk_drawing_area_new();
    gtk_widget_set_size_request(impl_->widget, config.size.width, config.size.height);

    GtkWidget* parent_container = static_cast<GtkWidget*>(parent.native_handle().get());
    gtk_fixed_put(GTK_FIXED(parent_container), impl_->widget, config.position.x, config.position.y);
    
    g_signal_connect(impl_->widget, "draw", G_CALLBACK(Impl::on_draw), impl_.get());
    
    gtk_widget_show(impl_->widget);

    impl_->timer_id = g_timeout_add(config.poll_interval_ms, Impl::on_timer, impl_.get());
}

PixelView::~PixelView() {
    if (impl_->timer_id > 0) g_source_remove(impl_->timer_id);
    impl_->close_channel(false);
    if (impl_->widget) {
        gtk_widget_destroy(impl_->widget);
        impl_->widget = nullptr;
    }
}

// ── Standard Boilerplate Geometry / Setters ───────────────────────────────────
void PixelView::set_size(Size size) { gtk_widget_set_size_request(impl_->widget, size.width, size.height); }
Size PixelView::get_size() const { 
    GtkAllocation a; gtk_widget_get_allocation(impl_->widget, &a); 
    return {a.width, a.height}; 
}
void PixelView::set_position(Point point) {
    GtkWidget* parent = gtk_widget_get_parent(impl_->widget);
    if (GTK_IS_FIXED(parent)) gtk_fixed_move(GTK_FIXED(parent), impl_->widget, point.x, point.y);
}
Point PixelView::get_position() const { return {0, 0}; /* Add logic matching View.cpp if needed */ }
void PixelView::set_min_size(Size size) { impl_->min_size = size; }
void PixelView::set_max_size(Size size) { impl_->max_size = size; }

void PixelView::show() { gtk_widget_show(impl_->widget); }
void PixelView::hide() { gtk_widget_hide(impl_->widget); }
bool PixelView::is_visible() const { return gtk_widget_get_visible(impl_->widget); }
bool PixelView::is_connected() const { return impl_->connected; }
uint64_t PixelView::get_frame_count() const { return impl_->frame_count; }

void PixelView::on_resize(std::function<void(Size)> fn) { impl_->cb_resize = std::move(fn); }
void PixelView::on_move(std::function<void(Point)> fn) { impl_->cb_move = std::move(fn); }
void PixelView::on_connect(std::function<void()> fn) { impl_->cb_connect = std::move(fn); }
void PixelView::on_disconnect(std::function<void()> fn) { impl_->cb_disconnect = std::move(fn); }
void PixelView::on_frame(std::function<void(FrameEvent&)> fn) { impl_->cb_frame = std::move(fn); }

NativeHandle PixelView::native_handle() const {
    return { impl_->widget, NativeHandleType::GtkWidget };
}

} // namespace ui