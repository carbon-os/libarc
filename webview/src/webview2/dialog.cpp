#include "WebViewImpl.hpp"
#include "string_util.hpp"

#include <webview/webview.hpp>
#include <shobjidl.h>
#include <optional>
#include <string>
#include <vector>

namespace webview {

using namespace detail;
using namespace Microsoft::WRL;

// Build COMDLG_FILTERSPEC array from FileFilter list.
// spec_strings holds the backing wstrings; specs points into them.
static void build_filter(
    const std::vector<FileFilter>& filters,
    std::vector<std::wstring>& spec_strings,
    std::vector<COMDLG_FILTERSPEC>& specs)
{
    for (auto& f : filters) {
        std::wstring name = to_wide(f.name);
        std::wstring pattern;
        for (auto& ext : f.extensions) {
            if (!pattern.empty()) pattern += L";";
            pattern += L"*." + to_wide(ext);
        }
        spec_strings.push_back(std::move(name));
        spec_strings.push_back(std::move(pattern));
    }
    // Build specs after all strings are stable in the vector.
    for (size_t i = 0; i + 1 < spec_strings.size(); i += 2) {
        COMDLG_FILTERSPEC s;
        s.pszName = spec_strings[i].c_str();
        s.pszSpec = spec_strings[i + 1].c_str();
        specs.push_back(s);
    }
}

std::optional<std::string> WebViewImpl::show_dialog(const FileDialog& d) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    if (d.mode == FileDialog::Mode::Save) {
        ComPtr<IFileSaveDialog> dlg;
        CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER,
                         IID_PPV_ARGS(&dlg));

        if (!d.title.empty()) dlg->SetTitle(to_wide(d.title).c_str());
        if (!d.default_path.empty()) {
            ComPtr<IShellItem> item;
            SHCreateItemFromParsingName(to_wide(d.default_path).c_str(),
                                        nullptr, IID_PPV_ARGS(&item));
            if (item) dlg->SetFolder(item.Get());
        }
        std::vector<std::wstring> strs;
        std::vector<COMDLG_FILTERSPEC> specs;
        build_filter(d.filters, strs, specs);
        if (!specs.empty())
            dlg->SetFileTypes((UINT)specs.size(), specs.data());

        if (SUCCEEDED(dlg->Show(hwnd_))) {
            ComPtr<IShellItem> result;
            dlg->GetResult(&result);
            if (result) {
                LPWSTR path = nullptr;
                result->GetDisplayName(SIGDN_FILESYSPATH, &path);
                if (path) {
                    std::string s = to_utf8(path);
                    CoTaskMemFree(path);
                    return s;
                }
            }
        }
        return std::nullopt;
    }

    // Open dialog
    ComPtr<IFileOpenDialog> dlg;
    CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                     IID_PPV_ARGS(&dlg));

    if (!d.title.empty()) dlg->SetTitle(to_wide(d.title).c_str());
    if (!d.default_path.empty()) {
        ComPtr<IShellItem> item;
        SHCreateItemFromParsingName(to_wide(d.default_path).c_str(),
                                    nullptr, IID_PPV_ARGS(&item));
        if (item) dlg->SetFolder(item.Get());
    }
    std::vector<std::wstring> strs;
    std::vector<COMDLG_FILTERSPEC> specs;
    build_filter(d.filters, strs, specs);
    if (!specs.empty())
        dlg->SetFileTypes((UINT)specs.size(), specs.data());

    if (SUCCEEDED(dlg->Show(hwnd_))) {
        ComPtr<IShellItem> result;
        dlg->GetResult(&result);
        if (result) {
            LPWSTR path = nullptr;
            result->GetDisplayName(SIGDN_FILESYSPATH, &path);
            if (path) {
                std::string s = to_utf8(path);
                CoTaskMemFree(path);
                return s;
            }
        }
    }
    return std::nullopt;
}

std::vector<std::string> WebViewImpl::show_dialog_multi(const FileDialog& d) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    ComPtr<IFileOpenDialog> dlg;
    CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                     IID_PPV_ARGS(&dlg));

    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_ALLOWMULTISELECT);

    if (!d.title.empty()) dlg->SetTitle(to_wide(d.title).c_str());
    if (!d.default_path.empty()) {
        ComPtr<IShellItem> item;
        SHCreateItemFromParsingName(to_wide(d.default_path).c_str(),
                                    nullptr, IID_PPV_ARGS(&item));
        if (item) dlg->SetFolder(item.Get());
    }
    std::vector<std::wstring> strs;
    std::vector<COMDLG_FILTERSPEC> specs;
    build_filter(d.filters, strs, specs);
    if (!specs.empty())
        dlg->SetFileTypes((UINT)specs.size(), specs.data());

    std::vector<std::string> result;
    if (SUCCEEDED(dlg->Show(hwnd_))) {
        ComPtr<IShellItemArray> items;
        dlg->GetResults(&items);
        if (items) {
            DWORD count = 0; items->GetCount(&count);
            for (DWORD i = 0; i < count; ++i) {
                ComPtr<IShellItem> item;
                items->GetItemAt(i, &item);
                if (item) {
                    LPWSTR path = nullptr;
                    item->GetDisplayName(SIGDN_FILESYSPATH, &path);
                    if (path) {
                        result.push_back(to_utf8(path));
                        CoTaskMemFree(path);
                    }
                }
            }
        }
    }
    return result;
}

// ── Public WebView forwarders ─────────────────────────────────────────────────

std::optional<std::string> WebView::dialog(FileDialog d)
    { return impl_->show_dialog(d); }

std::vector<std::string> WebView::dialog_multi(FileDialog d)
    { return impl_->show_dialog_multi(d); }

} // namespace webview