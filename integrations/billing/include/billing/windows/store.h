// billing/include/billing/windows/store.h
//
// Pure C++ public interface for Microsoft Store in-app purchases.
// Implemented in src/microsoft-store/Store.cpp via C++/WinRT
// (Windows.Services.Store namespace, Windows 10 1607+ / Build 14393+).
//
// Threading: all callbacks are invoked from an unspecified thread-pool thread.
// If you need UI-thread delivery, marshal inside your callback (e.g. PostMessage).
//
// The owner HWND supplied to the Store constructor is used by the Store purchase
// dialog and other modal UI. It must remain valid for the lifetime of the Store.
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>   // HWND

namespace billing::windows {

// ── Enums ─────────────────────────────────────────────────────────────────────

enum class ProductKind {
    Consumable,            // Store tracks balance; fulfilled via ReportConsumableFulfillmentAsync
    UnmanagedConsumable,   // App/server tracks fulfillment; quantity not tracked by Store
    NonConsumable,         // Durable, purchased once (no subscription info)
    AutoRenewableSubscription,
};

enum class PeriodUnit { Day, Week, Month, Year };

enum class PurchaseStatus {
    Succeeded,
    AlreadyPurchased,   // user already owns it — treated as success by ok()
    NotPurchased,       // user cancelled or flow failed
    NetworkError,
    ServerError,
};

enum class FulfillmentStatus {
    Succeeded,
    InsufficientQuantity,   // balance < requested quantity
    NetworkError,
    ServerError,
    Unknown,
};

// ── Value types ───────────────────────────────────────────────────────────────

struct SubscriptionPeriod {
    uint32_t   value;
    PeriodUnit unit;
};

struct Product {
    std::string store_id;       // e.g. "9NBLGGH4R315"
    std::string title;
    std::string description;
    std::string display_price;
    ProductKind kind;
    bool        is_in_user_collection = false;

    // Subscriptions only — nullopt for consumables / non-consumables
    std::optional<SubscriptionPeriod> subscription_period;
    bool        has_trial   = false;
    std::optional<SubscriptionPeriod> trial_period;
};

using TimePoint = std::chrono::system_clock::time_point;

// Derived from StoreAppLicense::AddOnLicenses.
struct Entitlement {
    std::string              store_id;        // matches ProductSpec::store_id
    std::string              sku_store_id;
    ProductKind              kind;
    bool                     is_active = false;
    std::optional<TimePoint> expires_at;      // nullopt = perpetual / not set
};

struct PurchaseResult {
    std::string   store_id;
    PurchaseStatus status;

    bool ok() const {
        return status == PurchaseStatus::Succeeded ||
               status == PurchaseStatus::AlreadyPurchased;
    }
};

// ── ProductSpec ───────────────────────────────────────────────────────────────

struct ProductSpec {
    std::string store_id;   // 12-char alphanumeric MS Store ID
    ProductKind kind;
};

// ── Store ─────────────────────────────────────────────────────────────────────

class Store {
public:
    // owner_hwnd must remain valid for the lifetime of this object.
    // WinRT / COM must already be initialized by the caller before construction
    // (e.g. via RoInitialize or winrt::init_apartment on the owning thread).
    explicit Store(HWND owner_hwnd);
    ~Store();

    Store(const Store&)            = delete;
    Store& operator=(const Store&) = delete;

    // ── Setup ─────────────────────────────────────────────────────────────────

    void register_products(std::vector<ProductSpec> specs);

    // ── Actions ───────────────────────────────────────────────────────────────

    void fetch_products();
    void purchase(const std::string& store_id);

    // Re-derives entitlements from the current license — no network round-trip.
    void restore_purchases();

    // Must be called after granting a consumable to the user.
    // tracking_id: a per-fulfillment-attempt UUID string (with or without braces).
    // quantity:    how many units to consume (default 1).
    void report_consumable_fulfilled(const std::string& store_id,
                                     const std::string& tracking_id,
                                     uint32_t           quantity = 1);

    // ── Queries ───────────────────────────────────────────────────────────────

    void current_entitlements(std::function<void(std::vector<Entitlement>)> fn);
    void check_entitlement(const std::string& store_id,
                           std::function<void(std::optional<Entitlement>)> fn);

    // ── Event sinks ───────────────────────────────────────────────────────────

    void on_products_fetched   (std::function<void(const std::vector<Product>&)>         fn);
    void on_purchase_completed (std::function<void(const PurchaseResult&)>               fn);
    void on_restore_completed  (std::function<void(const std::vector<PurchaseResult>&)>  fn);
    void on_entitlements_changed(std::function<void()>                                   fn);
    void on_consumable_fulfilled(std::function<void(const std::string&, FulfillmentStatus)> fn);

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

} // namespace billing::windows