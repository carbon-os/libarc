#pragma once
#include <string>
#include <optional>
#include <nlohmann/json.hpp>

namespace webview {

using json = nlohmann::json;

enum class ScriptInjectTime {
    DocumentStart,  // before any page scripts run
    DocumentEnd,    // after DOM is ready
};

struct EvalResult {
    json                     value;
    std::optional<std::string> error;
    bool ok() const { return !error.has_value(); }
};

} // namespace webview