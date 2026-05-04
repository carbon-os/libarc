#include <host/host.hpp>

#include "dispatcher.hpp"
#include "main_thread.hpp"
#include "registry.hpp"

#include <ipc/ipc.hpp>
#include <ui/ui.h>

#include <memory>
#include <thread>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

// Embedded mode: the module must export this symbol.
// Host calls it on a detached thread once the IPC server is listening.
// The module connects back as an ipc::Client using the provided channel_id.
extern "C" typedef void (*AppMainFn)(const char* channel_id);

namespace arc {

namespace {

std::string generate_channel_id() {
    // Simple unique ID — replace with a UUID library if preferred.
    static std::atomic<uint32_t> counter{0};
    return "arc-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()
        ^ (++counter));
}

} // namespace

struct Host::Impl {
    HostConfig config;

    WindowRegistry                     registry;
    std::unique_ptr<ipc::Server>       server;
    std::unique_ptr<CommandDispatcher> dispatcher;

    // Invisible sentinel window. Gives the native event loop something to run
    // against before the controller has created any application windows.
    std::unique_ptr<ui::Window> sentinel;
};

Host::Host(HostConfig config) : impl_(std::make_unique<Impl>()) {
    impl_->config = std::move(config);
}

Host::~Host() = default;

void Host::run() {
    auto& im = *impl_;

    // ── 1. Channel ID ─────────────────────────────────────────────────────────
    // Embedded mode generates its own ID if the caller didn't supply one.
    if (im.config.mode == HostMode::Embedded && im.config.channel_id.empty())
        im.config.channel_id = generate_channel_id();

    // ── 2. In-process transport registration (embedded mode only) ─────────────
    // Must happen before constructing ipc::Server so both sides share the
    // in-process channel rather than a socket or pipe.
    if (im.config.mode == HostMode::Embedded)
        ipc::register_inprocess(im.config.channel_id);

    // ── 3. Construct IPC server and dispatcher ────────────────────────────────
    im.server     = std::make_unique<ipc::Server>(im.config.channel_id);
    im.dispatcher = std::make_unique<CommandDispatcher>(*im.server, im.registry);

    // ── 4. Wire IPC callbacks → main thread ───────────────────────────────────
    // libipc delivers all callbacks on its internal I/O thread. Everything
    // that touches UI state must be marshalled to the main thread first.

    im.server->on_connect([&im]() {
        post_to_main_thread([&im]() {
            im.server->send({ {"type", "host.ready"} });
        });
    });

    im.server->on_disconnect([&im]() {
        // A disconnect is terminal — shut the application down.
        post_to_main_thread([&im]() {
            im.server->stop();
            quit_app();
        });
    });

    im.server->on_message([&im](ipc::Message msg) {
        post_to_main_thread([&im, msg = std::move(msg)]() mutable {
            im.dispatcher->dispatch(msg);
        });
    });

    im.server->on_error([](ipc::Error /*err*/) {
        // A transport error will surface as an on_disconnect shortly after.
    });

    // ── 5. Start accepting connections ────────────────────────────────────────
    im.server->listen();

    // ── 6. Load and start the module (embedded mode only) ─────────────────────
    // The module receives the channel ID and is expected to connect back as
    // an ipc::Client. It runs on its own detached thread and must not block.
    if (im.config.mode == HostMode::Embedded && !im.config.module_path.empty()) {
        AppMainFn app_main = nullptr;

#if defined(_WIN32)
        if (HMODULE lib = LoadLibraryA(im.config.module_path.c_str()))
            app_main = reinterpret_cast<AppMainFn>(GetProcAddress(lib, "AppMain"));
#else
        if (void* lib = dlopen(im.config.module_path.c_str(), RTLD_NOW | RTLD_GLOBAL))
            app_main = reinterpret_cast<AppMainFn>(dlsym(lib, "AppMain"));
#endif
        if (app_main) {
            const std::string cid = im.config.channel_id; // capture by value
            std::thread([app_main, cid]() {
                app_main(cid.c_str());
            }).detach();
        }
    }

    // ── 7. Platform-specific pre-loop setup ───────────────────────────────────
    init_main_thread_dispatch();

    // ── 8. Create the sentinel window and enter the native event loop ─────────
    // The sentinel is a hidden, zero-content window whose sole purpose is to
    // keep the event loop alive until the controller creates application windows.
    // It is never shown to the user.
    im.sentinel = std::make_unique<ui::Window>(ui::WindowConfig{
        .title     = "",
        .size      = { 1, 1 },
        .resizable = false,
        .style     = ui::WindowStyle::Borderless,
    });
    im.sentinel->hide();

    // Blocks here until quit_app() terminates the native event loop.
    im.sentinel->run();

    // ── 9. Teardown ───────────────────────────────────────────────────────────
    im.server->stop();
    ipc::unregister_inprocess(im.config.channel_id);
}

} // namespace arc