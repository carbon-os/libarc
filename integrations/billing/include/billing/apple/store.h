// billing/include/billing/apple/store.h
// Pure C++ — libarc and your app code never see Swift or ObjC.
// Store::Impl is intentionally incomplete here; defined in ShimStore.mm.

#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace billing::apple {

// ── Enums ─────────────────────────────────────────────────────────────────────

enum class ProductKind {
    Consumable,
    NonConsumable,
    AutoRenewableSubscription,
    NonRenewingSubscription,
};

enum class OfferType {
    Introductory,
    Promotional,
    WinBack,
    OfferCode,
};

enum class OfferPaymentMode {
    FreeTrial,
    PayAsYouGo,
    PayUpFront,
};

enum class PeriodUnit {
    Day, Week, Month, Year,
};

enum class SubscriptionState {
    Active,
    Expired,
    InBillingRetry,        // payment failed; Apple retrying — still has access
    InBillingGracePeriod,  // grace period after billing failure — still has access
    Revoked,               // Family Share revoked
};

enum class ExpirationReason {
    Cancelled,
    BillingError,
    PriceIncrease,
    ProductUnavailable,
    Unknown,
};

enum class PurchaseError {
    Cancelled,
    PaymentFailed,
    ProductNotFound,
    NotEntitled,
    PendingAuthorization,
    NetworkError,
    Unknown,
};

enum class RefundRequestStatus {
    Success,
    UserCancelled,
    Error,
};

// ── Value types ───────────────────────────────────────────────────────────────

struct SubscriptionPeriod {
    int        value;
    PeriodUnit unit;
};

struct Offer {
    std::string      id;
    OfferType        type;
    OfferPaymentMode payment_mode;
    std::string      display_price;
    SubscriptionPeriod period;
};

struct Product {
    std::string  id;
    std::string  title;
    std::string  description;
    std::string  display_price;
    ProductKind  kind;

    std::optional<SubscriptionPeriod> subscription_period;
    std::optional<Offer>              introductory_offer;
    std::vector<Offer>                promotional_offers;
    std::vector<Offer>                win_back_offers;
};

using TimePoint = std::chrono::system_clock::time_point;

struct Transaction {
    std::string  id;
    std::string  original_id;
    std::string  product_id;
    ProductKind  product_kind;

    TimePoint                 purchased_at;
    std::optional<TimePoint>  expires_at;
    std::optional<TimePoint>  revoked_at;
    std::optional<Offer>      redeemed_offer;

    bool family_shared = false;
    bool upgraded      = false;
};

struct Entitlement {
    std::string       product_id;
    SubscriptionState state;
    Transaction       transaction;

    std::optional<TimePoint>          expires_at;
    std::optional<ExpirationReason>   expiration_reason;
    bool                              will_auto_renew  = false;
    std::optional<std::string>        renewal_product_id;
};

struct PurchaseResult {
    std::string                  product_id;
    std::optional<Transaction>   transaction;
    std::optional<PurchaseError> error;

    bool ok() const { return !error.has_value(); }
};

// ── ProductSpec — registration ────────────────────────────────────────────────

struct ProductSpec {
    std::string product_id;
    ProductKind kind;
};

// ── Store ─────────────────────────────────────────────────────────────────────

class Store {
public:
     Store();
    ~Store(); // defined in ShimStore.mm — Impl must be complete at destruction

    Store(const Store&)            = delete;
    Store& operator=(const Store&) = delete;

    // Registration — call before fetch_products()
    void register_products(std::vector<ProductSpec> specs);

    // Async actions — results delivered via event hooks below
    void fetch_products();
    void purchase(const std::string& product_id,
                  std::optional<std::string> offer_id          = std::nullopt,
                  std::optional<std::string> jws_offer_signature = std::nullopt);
    void restore_purchases();

    // Entitlements — always re-derive from SK2, never cache across launches
    void current_entitlements(std::function<void(std::vector<Entitlement>)> fn);
    void check_entitlement(const std::string& product_id,
                           std::function<void(std::optional<Entitlement>)> fn);

    // Refund — presents Apple's native sheet; requires a visible window
    void request_refund(const std::string& transaction_id,
                        std::function<void(RefundRequestStatus)> fn);

    // Event hooks — all callbacks fire on the main thread
    void on_products_fetched(std::function<void(const std::vector<Product>&)> fn);
    void on_purchase_completed(std::function<void(const PurchaseResult&)> fn);
    void on_restore_completed(std::function<void(const std::vector<PurchaseResult>&)> fn);

    // Fires on any Transaction.updates event (renewals, cancellations, billing
    // failures, Family Share changes). Re-call current_entitlements() in response.
    void on_entitlements_changed(std::function<void()> fn);

    // Fires when user taps "Buy" on your App Store product page (promoted IAP).
    // Return false to defer the purchase; call purchase() later when ready.
    // Requires macOS 14.4+; no-op on earlier OS — registered but never fires.
    void on_promoted_iap(std::function<bool(const std::string& product_id)> fn);

private:
    struct Impl;                    // defined in ShimStore.mm
    std::unique_ptr<Impl> impl_;
};

} // namespace billing::apple