#include <gtk/gtk.h>
#include "ui/View.h"
#include "ui/Window.h"

// ── Impl ──────────────────────────────────────────────────────────────────────
namespace ui {

struct View::Impl {
    GtkWidget* view = nullptr;

    std::optional<Size> min_size;
    std::optional<Size> max_size;

    std::function<void(Size)>  cb_resize;
    std::function<void(Point)> cb_move;
    std::function<void()>      cb_focus;
    std::function<void()>      cb_blur;

    static gboolean on_size_allocate(GtkWidget* widget, GdkRectangle* alloc, gpointer data);
};

} // namespace ui

namespace ui {

gboolean View::Impl::on_size_allocate(GtkWidget* widget, GdkRectangle* alloc, gpointer data) {
    auto* impl = static_cast<Impl*>(data);
    if (impl->cb_resize) impl->cb_resize({alloc->width, alloc->height});
    // Move is handled via parent fixed container
    return FALSE;
}

// ── Constructor / Destructor ──────────────────────────────────────────────────

View::View(Window& parent, ViewConfig config) : impl_(std::make_unique<Impl>()) {
    impl_->view = gtk_event_box_new(); // Event box allows drawing backgrounds and capturing events
    
    impl_->min_size = config.min_size;
    impl_->max_size = config.max_size;

    gtk_widget_set_size_request(impl_->view, config.size.width, config.size.height);

    if (config.transparent) {
        gtk_event_box_set_visible_window(GTK_EVENT_BOX(impl_->view), FALSE);
    }

    GtkWidget* parent_container = static_cast<GtkWidget*>(parent.native_handle().get());
    gtk_fixed_put(GTK_FIXED(parent_container), impl_->view, config.position.x, config.position.y);
    gtk_widget_show_all(impl_->view);

    g_signal_connect(impl_->view, "size-allocate", G_CALLBACK(Impl::on_size_allocate), impl_.get());
}

View::~View() {
    if (impl_->view) {
        gtk_widget_destroy(impl_->view);
        impl_->view = nullptr;
    }
}

// ── Geometry ──────────────────────────────────────────────────────────────────

void View::set_size(Size size) {
    gtk_widget_set_size_request(impl_->view, size.width, size.height);
}

Size View::get_size() const {
    GtkAllocation alloc;
    gtk_widget_get_allocation(impl_->view, &alloc);
    return {alloc.width, alloc.height};
}

void View::set_position(Point point) {
    GtkWidget* parent = gtk_widget_get_parent(impl_->view);
    if (GTK_IS_FIXED(parent)) {
        gtk_fixed_move(GTK_FIXED(parent), impl_->view, point.x, point.y);
    }
}

Point View::get_position() const {
    GtkWidget* parent = gtk_widget_get_parent(impl_->view);
    gint x = 0, y = 0;
    if (GTK_IS_FIXED(parent)) {
        GValue x_val = G_VALUE_INIT, y_val = G_VALUE_INIT;
        g_value_init(&x_val, G_TYPE_INT);
        g_value_init(&y_val, G_TYPE_INT);
        gtk_container_child_get_property(GTK_CONTAINER(parent), impl_->view, "x", &x_val);
        gtk_container_child_get_property(GTK_CONTAINER(parent), impl_->view, "y", &y_val);
        x = g_value_get_int(&x_val);
        y = g_value_get_int(&y_val);
    }
    return {x, y};
}

void View::set_min_size(Size size) { impl_->min_size = size; }
void View::set_max_size(Size size) { impl_->max_size = size; }

// ── State ─────────────────────────────────────────────────────────────────────

void View::show()  { gtk_widget_show(impl_->view); }
void View::hide()  { gtk_widget_hide(impl_->view); }
void View::focus() { gtk_widget_grab_focus(impl_->view); }

bool View::is_visible() const { return gtk_widget_get_visible(impl_->view); }
bool View::is_focused() const { return gtk_widget_has_focus(impl_->view); }

// ── Appearance ────────────────────────────────────────────────────────────────

void View::set_effect(BackdropEffect effect) { /* Native blur backdrops unsupported */ }
void View::clear_effect() { }

// ── Events ────────────────────────────────────────────────────────────────────

void View::on_resize(std::function<void(Size)>  fn) { impl_->cb_resize = std::move(fn); }
void View::on_move  (std::function<void(Point)> fn) { impl_->cb_move   = std::move(fn); }
void View::on_focus (std::function<void()>      fn) { impl_->cb_focus  = std::move(fn); }
void View::on_blur  (std::function<void()>      fn) { impl_->cb_blur   = std::move(fn); }

// ── NativeHandle ──────────────────────────────────────────────────────────────

NativeHandle View::native_handle() const {
    return { impl_->view, NativeHandleType::GtkWidget };
}

} // namespace ui