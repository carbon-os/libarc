#include <host/host.hpp>

#include <cstring>
#include <iostream>
#include <string>

static void usage(const char* argv0) {
    std::cerr
        << "Usage: " << argv0
        << " --ipc-channel <id>"
           " [--mode <managed|embedded>]"
           " [--module <path>]\n"
        << "\n"
        << "  --ipc-channel  Channel ID for the IPC transport\n"
        << "                   managed:  required — provided by the controller\n"
        << "                   embedded: optional — auto-generated if omitted\n"
        << "  --mode         'managed' (default) or 'embedded'\n"
        << "  --module       Path to module .so/.dll (embedded mode only)\n"
        << "                   The module must export: void AppMain(const char* channel_id)\n";
}

int main(int argc, char* argv[]) {
    arc::HostMode mode = arc::HostMode::Managed;
    std::string   channel_id;
    std::string   module_path;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--ipc-channel") == 0 && i + 1 < argc) {
            channel_id = argv[++i];
        } else if (std::strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            ++i;
            if (std::strcmp(argv[i], "embedded") == 0)
                mode = arc::HostMode::Embedded;
            else if (std::strcmp(argv[i], "managed") != 0) {
                std::cerr << "Unknown mode: " << argv[i] << "\n";
                usage(argv[0]);
                return 1;
            }
        } else if (std::strcmp(argv[i], "--module") == 0 && i + 1 < argc) {
            module_path = argv[++i];
        } else {
            std::cerr << "Unknown argument: " << argv[i] << "\n";
            usage(argv[0]);
            return 1;
        }
    }

    // Managed mode requires a channel ID — the controller owns it.
    // Embedded mode can generate one itself if omitted.
    if (mode == arc::HostMode::Managed && channel_id.empty()) {
        usage(argv[0]);
        return 1;
    }

    arc::Host host(arc::HostConfig{
        .mode        = mode,
        .channel_id  = channel_id,
        .module_path = module_path,
    });

    host.run();
    return 0;
}