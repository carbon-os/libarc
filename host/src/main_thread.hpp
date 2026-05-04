#pragma once

#include <functional>

namespace arc {

// Post a callable onto the main (native event loop) thread.
// Thread-safe — safe to call from any thread, including the libipc I/O thread.
void post_to_main_thread(std::function<void()> fn);

// Signal the native event loop to exit. Safe to call from any thread.
void quit_app();

// Must be called once on the main thread before entering the event loop.
// No-op on Apple and Linux; installs the hidden dispatch window on Windows.
void init_main_thread_dispatch();

} // namespace arc