#pragma once
#include <string>
#include <cstdint>

namespace webview {

class DownloadEvent {
public:
    std::string id;
    std::string url;
    std::string suggested_filename;
    std::string destination;       // writable in on_download_start to override path
    int64_t     bytes_received = 0;
    int64_t     total_bytes    = -1;

    bool is_failed()    const { return failed_; }
    void cancel()             { cancelled_ = true; }

    // Internal
    void set_failed(bool f)  { failed_ = f; }
    bool is_cancelled() const { return cancelled_; }

private:
    bool failed_    = false;
    bool cancelled_ = false;
};

} // namespace webview