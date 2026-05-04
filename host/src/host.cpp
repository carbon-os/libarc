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

#if defined(__APPLE__)
#  include "apple_store_manager.hpp"
#endif

#if defined(_WIN32)
#  include "microsoft_store_manager.hpp"
#endif

extern "C" typedef void (*AppMainFn)(const char* channel_id);

namespace arc {

namespace {

std::string generate_channel_id() {
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

    std::unique_ptr<ui::Window> sentinel;
};

Host::Host(HostConfig config) : impl_(std::make_unique<Impl>()) {
    impl_->config = std::move(config);
}

Host::~Host() = default;

void Host::run() {
    auto& im = *impl_;

    // ── 1. Channel ID ─────────────────────────────────────────────────────────
    if (im.config.mode == HostMode::Embedded && im.config.channel_id.empty())
        im.config.channel_id = generate_channel_id();

    // ── 2. In-process transport (embedded mode only) ───────────────────────────
    if (im.config.mode == HostMode::Embedded)
        ipc::register_inprocess(im.config.channel_id);

    // ── 3. IPC server + dispatcher ────────────────────────────────────────────
    im.server     = std::make_unique<ipc::Server>(im.config.channel_id);
    im.dispatcher = std::make_unique<CommandDispatcher>(*im.server, im.registry);

    // ── 4. IPC callbacks ──────────────────────────────────────────────────────
    im.server->on_connect([&im]() {
        post_to_main_thread([&im]() {
            im.server->send({ {"type", "host.ready"} });
        });
    });

    im.server->on_disconnect([&im]() {
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

    im.server->on_error([](ipc::Error) {});

    // ── 5. Start listening ────────────────────────────────────────────────────
    im.server->listen();

    // ── 6. Load module (embedded mode only) ───────────────────────────────────
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
            const std::string cid = im.config.channel_id;
            std::thread([app_main, cid]() { app_main(cid.c_str()); }).detach();
        }
    }

    // ── 7. Platform dispatch setup ────────────────────────────────────────────
    init_main_thread_dispatch();

    // ── 8. Sentinel window ────────────────────────────────────────────────────
    im.sentinel = std::make_unique<ui::Window>(ui::WindowConfig{
        .title     = "",
        .size      = { 1, 1 },
        .resizable = false,
        .style     = ui::WindowStyle::Borderless,
    });
    im.sentinel->hide();

    // ── 9. Store managers ─────────────────────────────────────────────────────
    // Both are constructed after the sentinel so the Windows path has a valid
    // HWND. The emit lambda calls server::send() which is thread-safe, so
    // store callbacks don't need extra marshalling.
#if defined(__APPLE__)
    {
        auto mgr = std::make_unique<AppleStoreManager>(
            [&im](nlohmann::json j) { im.server->send(std::move(j)); });
        im.dispatcher->set_apple_store(std::move(mgr));
    }
#elif defined(_WIN32)
    {
        HWND hwnd = nullptr;
        auto nh   = im.sentinel->native_handle();
        if (nh.type() == ui::NativeHandleType::HWND)
            hwnd = reinterpret_cast<HWND>(nh.get());

        auto mgr = std::make_unique<MicrosoftStoreManager>(
            [&im](nlohmann::json j) { im.server->send(std::move(j)); }, hwnd);
        im.dispatcher->set_microsoft_store(std::move(mgr));
    }
#endif

    // ── 10. Event loop ────────────────────────────────────────────────────────
    im.sentinel->run();

    // ── 11. Teardown ──────────────────────────────────────────────────────────
    im.server->stop();
    ipc::unregister_inprocess(im.config.channel_id);
}

} // namespace arc