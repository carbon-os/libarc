#pragma once
#include <string>

namespace webview {

enum class PermissionType {
    Camera,
    Microphone,
    Geolocation,
    Notifications,
    ClipboardRead,
    Midi,
};

class PermissionRequest {
public:
    PermissionType permission;
    std::string    origin;

    void grant() { granted_ = true;  decided_ = true; }
    void deny()  { granted_ = false; decided_ = true; }

    bool is_decided() const { return decided_; }
    bool is_granted() const { return granted_; }

private:
    bool decided_ = false;
    bool granted_ = false;
};

} // namespace webview