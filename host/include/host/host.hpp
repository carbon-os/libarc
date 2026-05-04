#pragma once

#include <memory>
#include <string>

namespace arc {

enum class HostMode {
    Managed,   // host is a subprocess; controller is an external process that
               // connects via the channel ID passed on the command line
    Embedded,  // host owns the channel ID; loads a module (.so / .dll) and
               // passes the channel ID to its AppMain entry point
};

struct HostConfig {
    HostMode    mode        = HostMode::Managed;
    std::string channel_id; // Managed:  required, provided by the controller
                            // Embedded: auto-generated if empty
    std::string module_path;// Embedded only: path to module exporting AppMain
};

// Top-level host object. Owns the IPC server, registry, and dispatcher.
// run() enters the native event loop and blocks until shutdown.
class Host {
public:
    explicit Host(HostConfig config);
    ~Host();

    Host(const Host&)            = delete;
    Host& operator=(const Host&) = delete;

    void run();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace arc