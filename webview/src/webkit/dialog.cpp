#include "WebViewImpl.hpp"

namespace webview {

static void apply_filters(GtkFileChooser* chooser, const std::vector<FileFilter>& filters) {
    for (const auto& f : filters) {
        GtkFileFilter* filter = gtk_file_filter_new();
        gtk_file_filter_set_name(filter, f.name.c_str());
        for (const auto& ext : f.extensions) {
            std::string pattern = "*." + ext;
            gtk_file_filter_add_pattern(filter, pattern.c_str());
        }
        gtk_file_chooser_add_filter(chooser, filter);
    }
}

std::optional<std::string> WebViewImpl::show_dialog(const FileDialog& d) {
    GtkFileChooserAction action = (d.mode == FileDialog::Mode::Save) 
        ? GTK_FILE_CHOOSER_ACTION_SAVE 
        : GTK_FILE_CHOOSER_ACTION_OPEN;

    const char* accept_label = (d.mode == FileDialog::Mode::Save) ? "_Save" : "_Open";

    GtkWidget* dialog = gtk_file_chooser_dialog_new(
        d.title.empty() ? "Select File" : d.title.c_str(),
        nullptr, 
        action,
        "_Cancel", GTK_RESPONSE_CANCEL,
        accept_label, GTK_RESPONSE_ACCEPT,
        nullptr);

    apply_filters(GTK_FILE_CHOOSER(dialog), d.filters);

    if (!d.default_path.empty()) {
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), d.default_path.c_str());
    }

    std::optional<std::string> result = std::nullopt;

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char* filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (filename) {
            result = std::string(filename);
            g_free(filename);
        }
    }

    gtk_widget_destroy(dialog);
    return result;
}

std::vector<std::string> WebViewImpl::show_dialog_multi(const FileDialog& d) {
    GtkWidget* dialog = gtk_file_chooser_dialog_new(
        d.title.empty() ? "Select Files" : d.title.c_str(),
        nullptr,
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open", GTK_RESPONSE_ACCEPT,
        nullptr);

    gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dialog), TRUE);
    apply_filters(GTK_FILE_CHOOSER(dialog), d.filters);

    if (!d.default_path.empty()) {
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), d.default_path.c_str());
    }

    std::vector<std::string> result;

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        GSList* filenames = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(dialog));
        for (GSList* node = filenames; node != nullptr; node = node->next) {
            char* filename = static_cast<char*>(node->data);
            result.push_back(std::string(filename));
            g_free(filename);
        }
        g_slist_free(filenames);
    }

    gtk_widget_destroy(dialog);
    return result;
}

// ── Public WebView forwarders ─────────────────────────────────────────────────

std::optional<std::string> WebView::dialog(FileDialog d)
    { return impl_->show_dialog(d); }

std::vector<std::string> WebView::dialog_multi(FileDialog d)
    { return impl_->show_dialog_multi(d); }

} // namespace webview