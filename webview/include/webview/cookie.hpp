#pragma once
#include <string>
#include <optional>
#include <cstdint>

namespace webview {

struct Cookie {
    std::string             name;
    std::string             value;
    std::string             domain;
    std::string             path      = "/";
    std::optional<int64_t>  expires;     // Unix timestamp; nullopt = session cookie
    bool                    secure    = false;
    bool                    http_only = false;
};

} // namespace webview