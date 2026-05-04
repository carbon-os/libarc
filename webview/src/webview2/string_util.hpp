#pragma once
#include <windows.h>
#include <objidl.h>
#include <string>
#include <vector>
#include <cstdint>

namespace webview::detail {

inline std::wstring to_wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring ws(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), ws.data(), n);
    return ws;
}

inline std::string to_utf8(LPCWSTR ws) {
    if (!ws) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, ws, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string s(n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws, -1, s.data(), n, nullptr, nullptr);
    return s;
}

inline std::string to_utf8(const std::wstring& ws) {
    return to_utf8(ws.c_str());
}

// RAII wrapper for CoTaskMemFree'd strings returned by WebView2 APIs.
struct CoStr {
    LPWSTR ptr = nullptr;
    ~CoStr() { if (ptr) CoTaskMemFree(ptr); }
    std::string str() const { return to_utf8(ptr); }
};

// Read all bytes from a COM IStream (seeks to start first).
inline std::vector<uint8_t> read_istream(IStream* stream) {
    if (!stream) return {};
    LARGE_INTEGER zero{};
    stream->Seek(zero, STREAM_SEEK_SET, nullptr);
    std::vector<uint8_t> result;
    uint8_t buf[8192];
    ULONG read = 0;
    HRESULT hr;
    do {
        hr = stream->Read(buf, sizeof(buf), &read);
        if (read > 0) result.insert(result.end(), buf, buf + read);
    } while (hr == S_OK && read > 0);
    return result;
}

// Build a flat header string for CreateWebResourceResponse:
// "Name: Value\r\nName: Value\r\n"
inline std::wstring make_response_headers(
    int status,
    const std::string& mime,
    const std::map<std::string, std::string>& extra = {})
{
    std::wstring h;
    auto add = [&](const std::string& k, const std::string& v) {
        h += to_wide(k) + L": " + to_wide(v) + L"\r\n";
    };
    add("Content-Type", mime);
    add("Access-Control-Allow-Origin", "*");
    add("Cache-Control", "no-store");
    for (auto& [k, v] : extra) add(k, v);
    return h;
}

} // namespace webview::detail