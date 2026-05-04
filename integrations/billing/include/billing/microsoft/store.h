// billing/include/billing/microsoft/store.h
#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <optional>

// WIN32_LEAN_AND_MEAN is defined via target_compile_definitions in CMake.
// Defining it here too causes C4005 macro redefinition warnings.
#include <windows.h>

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
    NotPurchased,
    NetworkError,
    ServerError,
    Unknown
};

struct PurchaseResult {
    PurchaseStatus status;
    std::string    extended_error_message;
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
    static std::unique_ptr<Store> create(HWND owner_hwnd);

    virtual ~Store() = default;

    virtual void get_products(const std::vector<std::string>& store_ids,
                              std::function<void(std::vector<Product>)> callback) = 0;

    virtual void purchase(const std::string& store_id,
                          std::function<void(PurchaseResult)> callback) = 0;

    virtual void report_consumable_fulfilled(const std::string& store_id,
                                             uint32_t quantity,
                                             const std::string& tracking_id,
                                             std::function<void(ConsumeStatus)> callback) = 0;

    virtual void get_owned_store_ids(std::function<void(std::vector<std::string>)> callback) = 0;
};

} // namespace billing::microsoft