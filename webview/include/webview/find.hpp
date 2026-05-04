#pragma once

namespace webview {

struct FindOptions {
    bool case_sensitive = false;
    bool wrap           = true;
};

struct FindResult {
    int match_count  = 0;
    int active_match = 0;   // 0-based index of highlighted match
};

} // namespace webview