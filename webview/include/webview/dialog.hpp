#pragma once
#include <string>
#include <vector>

namespace webview {

struct FileFilter {
    std::string              name;
    std::vector<std::string> extensions;   // e.g. {"png","jpg"}
};

struct FileDialog {
    enum class Mode { Open, Save, OpenMultiple };

    Mode                     mode         = Mode::Open;
    std::vector<FileFilter>  filters;
    std::string              default_path;
    std::string              title;
};

} // namespace webview