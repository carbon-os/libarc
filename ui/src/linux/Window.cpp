#include <gtk/gtk.h>
#include "ui/Window.h"

#include <algorithm>
#include <mutex>
#include <stdexcept>
#include <string>

// ── GTK Initialization ────────────────────────────────────────────────────────
static void ensure_gtk_initialized() {
    static std::once_flag flag;
    std::call_once(flag, [] {
        gtk_init_check(nullptr, nullptr);
    });
}

// ── Impl ──────────────────────────────────────────────────────────────────────
namespace ui {

struct Window::Impl {
    GtkWidget* window = nullptr;
    GtkWidget* fixed  = nullptr; // Use GtkFixed for absolute positioning of children

    std::optional<Size> min_size;
    std::optional<Size> max_size;
    bool fullscreen = false;

    std::function<void(Size)>        cb_resize;
    std::function<void(Point)>       cb_move;
    std::function<void()>            cb_focus;
    std::function<void()>            cb_blur;
    std::function<void(WindowState)> cb_state_change;
    std::function<bool()>            cb_close;
    std::function<void(DropEvent&)>  cb_drop;

    // Signal handlers
    static gboolean on_configure(GtkWidget* widget, GdkEventConfigure* event, gpointer data);
    static gboolean on_focus_in(GtkWidget* widget, GdkEventFocus* event, gpointer data);
    static gboolean on_focus_out(GtkWidget* widget, GdkEventFocus* event, gpointer data);
    static gboolean on_window_state(GtkWidget* widget, GdkEventWindowState* event, gpointer data);
    static gboolean on_delete(GtkWidget* widget, GdkEvent* event, gpointer data);
    static void     on_drag_data_received(GtkWidget* widget, GdkDragContext* context, 
                                          gint x, gint y, GtkSelectionData* sel_data, 
                                          guint info, guint time, gpointer data);
};

} // namespace ui

// ── Signal Implementations ────────────────────────────────────────────────────
namespace ui {

gboolean Window::Impl::on_configure(GtkWidget* widget, GdkEventConfigure* event, gpointer data) {
    auto* impl = static_cast<Impl*>(data);
    if (impl->cb_resize) impl->cb_resize({event->width, event->height});
    if (impl->cb_move)   impl->cb_move({event->x, event->y});
    return FALSE;
}

gboolean Window::Impl::on_focus_in(GtkWidget* widget, GdkEventFocus* event, gpointer data) {
    auto* impl = static_cast<Impl*>(data);
    if (impl->cb_focus) impl->cb_focus();
    return FALSE;
}

gboolean Window::Impl::on_focus_out(GtkWidget* widget, GdkEventFocus* event, gpointer data) {
    auto* impl = static_cast<Impl*>(data);
    if (impl->cb_blur) impl->cb_blur();
    return FALSE;
}

gboolean Window::Impl::on_window_state(GtkWidget* widget, GdkEventWindowState* event, gpointer data) {
    auto* impl = static_cast<Impl*>(data);
    if (!impl->cb_state_change) return FALSE;

    if (event->new_window_state & GDK_WINDOW_STATE_FULLSCREEN) {
        impl->fullscreen = true;
        impl->cb_state_change(WindowState::Fullscreen);
    } else if (event->new_window_state & GDK_WINDOW_STATE_ICONIFIED) {
        impl->cb_state_change(WindowState::Minimized);
    } else if (event->new_window_state & GDK_WINDOW_STATE_MAXIMIZED) {
        impl->cb_state_change(WindowState::Maximized);
    } else {
        impl->fullscreen = false;
        impl->cb_state_change(WindowState::Normal);
    }
    return FALSE;
}

gboolean Window::Impl::on_delete(GtkWidget* widget, GdkEvent* event, gpointer data) {
    auto* impl = static_cast<Impl*>(data);
    bool should_close = impl->cb_close ? impl->cb_close() : true;
    if (should_close) {
        gtk_main_quit();
    }
    return !should_close; // TRUE stops the event (prevents close)
}

void Window::Impl::on_drag_data_received(GtkWidget*, GdkDragContext*, gint x, gint y,
                                         GtkSelectionData* sel_data, guint, guint, gpointer data) {
    auto* impl = static_cast<Impl*>(data);
    if (!impl->cb_drop) return;

    gchar** uris = gtk_selection_data_get_uris(sel_data);
    if (!uris) return;

    DropEvent ev;
    ev.position = {x, y};
    for (int i = 0; uris[i] != nullptr; ++i) {
        gchar* filename = g_filename_from_uri(uris[i], nullptr, nullptr);
        if (filename) {
            ev.paths.push_back(filename);
            g_free(filename);
        }
    }
    g_strfreev(uris);
    impl->cb_drop(ev);
}

// ── Constructor / Destructor ──────────────────────────────────────────────────

Window::Window(WindowConfig config) : impl_(std::make_unique<Impl>()) {
    ensure_gtk_initialized();

    impl_->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    
    // Apply styling
    gtk_window_set_title(GTK_WINDOW(impl_->window), config.title.c_str());
    gtk_window_set_default_size(GTK_WINDOW(impl_->window), config.size.width, config.size.height);
    gtk_window_set_resizable(GTK_WINDOW(impl_->window), config.resizable ? TRUE : FALSE);

    if (config.style == WindowStyle::Borderless) {
        gtk_window_set_decorated(GTK_WINDOW(impl_->window), FALSE);
    }

    if (config.always_on_top) {
        gtk_window_set_keep_above(GTK_WINDOW(impl_->window), TRUE);
    }

    // Set up the fixed container to act as the content view
    impl_->fixed = gtk_fixed_new();
    gtk_container_add(GTK_CONTAINER(impl_->window), impl_->fixed);

    // Geometry constraints
    impl_->min_size = config.min_size;
    impl_->max_size = config.max_size;
    if (config.min_size || config.max_size) {
        GdkGeometry geo = {};
        GdkWindowHints hints = (GdkWindowHints)0;
        if (config.min_size) {
            geo.min_width = config.min_size->width;
            geo.min_height = config.min_size->height;
            hints = (GdkWindowHints)(hints | GDK_HINT_MIN_SIZE);
        }
        if (config.max_size) {
            geo.max_width = config.max_size->width;
            geo.max_height = config.max_size->height;
            hints = (GdkWindowHints)(hints | GDK_HINT_MAX_SIZE);
        }
        gtk_window_set_geometry_hints(GTK_WINDOW(impl_->window), nullptr, &geo, hints);
    }

    // Transparent window setup
    if (config.transparent) {
        gtk_widget_set_app_paintable(impl_->window, TRUE);
        GdkScreen* screen = gtk_widget_get_screen(impl_->window);
        GdkVisual* visual = gdk_screen_get_rgba_visual(screen);
        if (visual && gdk_screen_is_composited(screen)) {
            gtk_widget_set_visual(impl_->window, visual);
        }
    }

    // Connect signals
    g_signal_connect(impl_->window, "configure-event", G_CALLBACK(Impl::on_configure), impl_.get());
    g_signal_connect(impl_->window, "focus-in-event", G_CALLBACK(Impl::on_focus_in), impl_.get());
    g_signal_connect(impl_->window, "focus-out-event", G_CALLBACK(Impl::on_focus_out), impl_.get());
    g_signal_connect(impl_->window, "window-state-event", G_CALLBACK(Impl::on_window_state), impl_.get());
    g_signal_connect(impl_->window, "delete-event", G_CALLBACK(Impl::on_delete), impl_.get());

    if (config.position) {
        gtk_window_move(GTK_WINDOW(impl_->window), config.position->x, config.position->y);
    } else {
        gtk_window_set_position(GTK_WINDOW(impl_->window), GTK_WIN_POS_CENTER);
    }

    gtk_widget_show_all(impl_->window);
}

Window::~Window() {
    if (impl_->window) {
        gtk_widget_destroy(impl_->window);
        impl_->window = nullptr;
    }
}

// ── Geometry ──────────────────────────────────────────────────────────────────

void Window::set_size(Size size) {
    gtk_window_resize(GTK_WINDOW(impl_->window), size.width, size.height);
}

Size Window::get_size() const {
    gint w, h;
    gtk_window_get_size(GTK_WINDOW(impl_->window), &w, &h);
    return {w, h};
}

void Window::set_position(Point point) {
    gtk_window_move(GTK_WINDOW(impl_->window), point.x, point.y);
}

Point Window::get_position() const {
    gint x, y;
    gtk_window_get_position(GTK_WINDOW(impl_->window), &x, &y);
    return {x, y};
}

void Window::center() {
    gtk_window_set_position(GTK_WINDOW(impl_->window), GTK_WIN_POS_CENTER);
}

void Window::set_min_size(Size size) { /* Implement via geometry hints update */ }
void Window::set_max_size(Size size) { /* Implement via geometry hints update */ }

// ── State ─────────────────────────────────────────────────────────────────────

void Window::show()     { gtk_widget_show_all(impl_->window); }
void Window::hide()     { gtk_widget_hide(impl_->window); }
void Window::focus()    { gtk_window_present(GTK_WINDOW(impl_->window)); }
void Window::minimize() { gtk_window_iconify(GTK_WINDOW(impl_->window)); }
void Window::maximize() { gtk_window_maximize(GTK_WINDOW(impl_->window)); }
void Window::restore()  { gtk_window_deiconify(GTK_WINDOW(impl_->window)); }

void Window::run() {
    gtk_main();
}

void Window::set_fullscreen(bool fullscreen) {
    if (fullscreen) gtk_window_fullscreen(GTK_WINDOW(impl_->window));
    else gtk_window_unfullscreen(GTK_WINDOW(impl_->window));
}

bool Window::is_fullscreen() const { return impl_->fullscreen; }

bool Window::is_visible() const { return gtk_widget_get_visible(impl_->window); }

bool Window::is_focused() const { return gtk_window_has_toplevel_focus(GTK_WINDOW(impl_->window)); }

WindowState Window::get_state() const {
    if (impl_->fullscreen) return WindowState::Fullscreen;
    return WindowState::Normal; // Min/Max query requires GdkWindow state check
}

// ── Appearance ────────────────────────────────────────────────────────────────

void Window::set_title(std::string title) {
    gtk_window_set_title(GTK_WINDOW(impl_->window), title.c_str());
}

std::string Window::get_title() const {
    const gchar* title = gtk_window_get_title(GTK_WINDOW(impl_->window));
    return title ? std::string(title) : "";
}

void Window::set_always_on_top(bool on_top) {
    gtk_window_set_keep_above(GTK_WINDOW(impl_->window), on_top ? TRUE : FALSE);
}

void Window::set_effect(BackdropEffect effect) { /* Native blur backdrops unsupported in GTK3 */ }
void Window::clear_effect() { }

// ── Events ────────────────────────────────────────────────────────────────────

void Window::on_resize      (std::function<void(Size)>        fn) { impl_->cb_resize       = std::move(fn); }
void Window::on_move        (std::function<void(Point)>       fn) { impl_->cb_move         = std::move(fn); }
void Window::on_focus       (std::function<void()>            fn) { impl_->cb_focus        = std::move(fn); }
void Window::on_blur        (std::function<void()>            fn) { impl_->cb_blur         = std::move(fn); }
void Window::on_state_change(std::function<void(WindowState)> fn) { impl_->cb_state_change = std::move(fn); }
void Window::on_close       (std::function<bool()>            fn) { impl_->cb_close        = std::move(fn); }
void Window::on_drop        (std::function<void(DropEvent&)>  fn) {
    impl_->cb_drop = std::move(fn);
    gtk_drag_dest_set(impl_->window, GTK_DEST_DEFAULT_ALL, nullptr, 0, GDK_ACTION_COPY);
    gtk_drag_dest_add_uri_targets(impl_->window);
    g_signal_connect(impl_->window, "drag-data-received", G_CALLBACK(Impl::on_drag_data_received), impl_.get());
}

// ── NativeHandle ──────────────────────────────────────────────────────────────

NativeHandle Window::native_handle() const {
    // Return the internal fixed layout container so children can attach directly
    return { impl_->fixed, NativeHandleType::GtkWidget };
}

} // namespace ui