#pragma once
#if defined(__APPLE__) || defined(_WIN32)

#include <functional>
#include <string>

#include <nlohmann/json.hpp>

#if defined(__APPLE__)
#  include <billing/apple/store.h>
#elif defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <billing/microsoft/store.h>
#endif

namespace arc {

// ─────────────────────────────────────────────────────────────────────────────
// BillingManager
//
// Owns the platform store object and bridges between the IPC JSON protocol
// and the billing:: API.  All emit_fn calls may originate from a background
// thread (billing callbacks are async); the function must therefore be
// thread-safe.  In practice the Host wires it to ipc::Server::send(), which
// is thread-safe.
//
// On Apple  — wraps billing::apple::Store (always available once constructed).
// On Windows — wraps billing::microsoft::Store, which requires an owner HWND
//              for the purchase UI; the sentinel window's HWND is used.
// ─────────────────────────────────────────────────────────────────────────────
class BillingManager {
public:
#if defined(_WIN32)
    BillingManager(std::function<void(nlohmann::json)> emit_fn, HWND owner_hwnd);
#else
    explicit BillingManager(std::function<void(nlohmann::json)> emit_fn);
#endif

    ~BillingManager() = default;

    BillingManager(const BillingManager&)            = delete;
    BillingManager& operator=(const BillingManager&) = delete;

    // Entry point — called by CommandDispatcher for every "billing.*" message.
    // |type| is the already-extracted "type" string; |j| is the full message.
    void dispatch(const std::string& type, const nlohmann::json& j);

private:
    // ── Command handlers ──────────────────────────────────────────────────────
    void on_fetch_products(const nlohmann::json& j);
    void on_purchase(const nlohmann::json& j);
    void on_restore_purchases();
    void on_current_entitlements();
    void on_check_entitlement(const nlohmann::json& j);
    void on_request_refund(const nlohmann::json& j);

    void emit(nlohmann::json evt);

    std::function<void(nlohmann::json)> emit_;

#if defined(__APPLE__)
    billing::apple::Store store_;
#elif defined(_WIN32)
    std::unique_ptr<billing::microsoft::Store> store_; // null if HWND was null
#endif
};

} // namespace arc

#endif // __APPLE__ || _WIN32