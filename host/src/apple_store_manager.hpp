#pragma once
#if defined(__APPLE__)

#include <functional>
#include <string>

#include <nlohmann/json.hpp>
#include <billing/apple/store.h>

namespace arc {

// Owns the Apple StoreKit store object and bridges between the IPC JSON
// protocol (apple.store.*) and the billing::apple API.
// All emit_fn calls are thread-safe — billing callbacks arrive on arbitrary
// threads; ipc::Server::send() handles the locking internally.
class AppleStoreManager {
public:
    explicit AppleStoreManager(std::function<void(nlohmann::json)> emit_fn);
    ~AppleStoreManager() = default;

    AppleStoreManager(const AppleStoreManager&)            = delete;
    AppleStoreManager& operator=(const AppleStoreManager&) = delete;

    // Entry point — called by CommandDispatcher for every "apple.store.*" message.
    // |type| is the already-extracted "type" string; |j| is the full message.
    void dispatch(const std::string& type, const nlohmann::json& j);

private:
    void on_fetch_products(const nlohmann::json& j);
    void on_purchase(const nlohmann::json& j);
    void on_restore_purchases();
    void on_current_entitlements();
    void on_check_entitlement(const nlohmann::json& j);
    void on_request_refund(const nlohmann::json& j);

    void emit(nlohmann::json evt);

    std::function<void(nlohmann::json)> emit_;
    billing::apple::Store               store_;
};

} // namespace arc

#endif // __APPLE__