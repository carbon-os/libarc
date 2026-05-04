#include "main_thread.hpp"

// ─── macOS ────────────────────────────────────────────────────────────────────
#if defined(__APPLE__)

#include <dispatch/dispatch.h>
#include <objc/message.h>
#include <objc/runtime.h>

void arc::post_to_main_thread(std::function<void()> fn) {
    // Heap-allocate so the block can outlive this call.
    auto* fp = new std::function<void()>(std::move(fn));
    dispatch_async(dispatch_get_main_queue(), ^{
        (*fp)();
        delete fp;
    });
}

void arc::quit_app() {
    post_to_main_thread([] {
        id app = reinterpret_cast<id(*)(Class, SEL)>(objc_msgSend)(
            objc_getClass("NSApplication"),
            sel_registerName("sharedApplication"));
        reinterpret_cast<void(*)(id, SEL, id)>(objc_msgSend)(
            app, sel_registerName("terminate:"), nullptr);
    });
}

void arc::init_main_thread_dispatch() {}

// ─── Windows ──────────────────────────────────────────────────────────────────
#elif defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mutex>
#include <queue>

// We route work through a hidden HWND_MESSAGE window so we can use PostMessage
// to wake the Win32 message loop on the main thread.
static HWND   s_dispatch_hwnd = nullptr;
static UINT   s_dispatch_msg  = 0;

static std::mutex                        s_queue_mutex;
static std::queue<std::function<void()>> s_queue;

static LRESULT CALLBACK DispatchWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == s_dispatch_msg) {
        // Drain the entire queue on each wake — avoids messages piling up.
        for (;;) {
            std::function<void()> fn;
            {
                std::lock_guard<std::mutex> lock(s_queue_mutex);
                if (s_queue.empty()) break;
                fn = std::move(s_queue.front());
                s_queue.pop();
            }
            fn();
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void arc::init_main_thread_dispatch() {
    WNDCLASSW wc   = {};
    wc.lpfnWndProc = DispatchWndProc;
    wc.hInstance   = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"ArcHostDispatch";
    RegisterClassW(&wc);

    s_dispatch_hwnd = CreateWindowExW(
        0, L"ArcHostDispatch", nullptr, 0,
        0, 0, 0, 0,
        HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);

    s_dispatch_msg = RegisterWindowMessageW(L"ArcHostDispatch");
}

void arc::post_to_main_thread(std::function<void()> fn) {
    {
        std::lock_guard<std::mutex> lock(s_queue_mutex);
        s_queue.push(std::move(fn));
    }
    if (s_dispatch_hwnd)
        PostMessageW(s_dispatch_hwnd, s_dispatch_msg, 0, 0);
}

void arc::quit_app() {
    post_to_main_thread([] { PostQuitMessage(0); });
}

// ─── Linux ────────────────────────────────────────────────────────────────────
#elif defined(__linux__)

#include <gtk/gtk.h>

struct IdleClosure {
    std::function<void()> fn;
};

static gboolean idle_dispatch(gpointer data) {
    auto* c = static_cast<IdleClosure*>(data);
    c->fn();
    delete c;
    return G_SOURCE_REMOVE; // run once, then remove
}

void arc::init_main_thread_dispatch() {}

void arc::post_to_main_thread(std::function<void()> fn) {
    g_idle_add(idle_dispatch, new IdleClosure{ std::move(fn) });
}

void arc::quit_app() {
    post_to_main_thread([] { gtk_main_quit(); });
}

#endif