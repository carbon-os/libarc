#pragma once
#if defined(_WIN32)

#include <functional>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <billing/microsoft/store.h>

namespace arc {

// Owns the Microsoft Store object and bridges between the IPC JSON protocol
// (microsoft.store.*) and the billing::microsoft API.
// All emit_fn calls are thread-safe — store callbacks arrive on arbitrary
// threads; ipc::Server::send() handles the locking internally.
//
// store_ stays null if owner_hwnd is null (e.g. very early startup);
// every command handler is a guarded no-op in that case.
class MicrosoftStoreManager {
public:
    MicrosoftStoreManager(std::function<void(nlohmann::json)> emit_fn, HWND owner_hwnd);
    ~MicrosoftStoreManager() = default;

    MicrosoftStoreManager(const MicrosoftStoreManager&)            = delete;
    MicrosoftStoreManager& operator=(const MicrosoftStoreManager&) = delete;

    // Entry point — called by CommandDispatcher for every "microsoft.store.*" message.
    // |type| is the already-extracted "type" string; |j| is the full message.
    void dispatch(const std::string& type, const nlohmann::json& j);

private:
    void on_fetch_products(const nlohmann::json& j);
    void on_purchase(const nlohmann::json& j);
    void on_get_owned();
    void on_check_entitlement(const nlohmann::json& j);
    void on_report_consumable(const nlohmann::json& j);

    void emit(nlohmann::json evt);

    std::function<void(nlohmann::json)>        emit_;
    std::unique_ptr<billing::microsoft::Store> store_;
};

} // namespace arc

#endif // _WIN32