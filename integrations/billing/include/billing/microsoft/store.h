// billing/include/billing/microsoft/store.h
#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <optional>

#define WIN32_LEAN_AND_MEAN
#include <windows.h> // HWND required for dialog parenting

namespace billing::microsoft {

enum class ProductType {
    App,
    Game,
    Consumable,
    UnmanagedConsumable,
    Durable
};

struct Product {
    std::string store_id;
    std::string title;
    std::string description;
    std::string formatted_price;
    ProductType type;
    bool        is_owned;
};

enum class PurchaseStatus {
    Succeeded,
    AlreadyPurchased,
    NotPurchased, // User cancelled or aborted
    NetworkError,
    ServerError,
    Unknown
};

struct PurchaseResult {
    PurchaseStatus status;
    std::string    extended_error_message; // Useful for logging HRESULT messages
};

enum class ConsumeStatus {
    Succeeded,
    InsufficientQuantity,
    NetworkError,
    ServerError,
    Unknown
};

class Store {
public:
    // Creates the Store instance and ties modal dialogs to the provided owner HWND.
    // COM/WinRT must be initialized on the calling thread before execution.
    static std::unique_ptr<Store> create(HWND owner_hwnd);

    virtual ~Store() = default;

    // Fetch rich product data directly by Store IDs
    virtual void get_products(const std::vector<std::string>& store_ids,
                              std::function<void(std::vector<Product>)> callback) = 0;

    // Trigger the Windows UI purchase dialog
    virtual void purchase(const std::string& store_id,
                          std::function<void(PurchaseResult)> callback) = 0;

    // Fulfill a managed consumable. `tracking_id` should be a unique UUID string 
    // to prevent double-consumption on network failures.
    virtual void report_consumable_fulfilled(const std::string& store_id,
                                             uint32_t quantity,
                                             const std::string& tracking_id,
                                             std::function<void(ConsumeStatus)> callback) = 0;

    // Returns a list of Store IDs for all active, owned Add-ons (Durables & Subscriptions)
    virtual void get_owned_store_ids(std::function<void(std::vector<std::string>)> callback) = 0;
};

} // namespace billing::microsoft