// billing/include/billing/apple/store.h
#pragma once

#include <chrono>
#include <cstdint>
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
    InBillingRetry,
    InBillingGracePeriod,
    Revoked,
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
    std::string        id;
    OfferType          type;
    OfferPaymentMode   payment_mode;
    std::string        display_price;
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

    TimePoint                purchased_at;
    std::optional<TimePoint> expires_at;
    std::optional<TimePoint> revoked_at;
    std::optional<Offer>     redeemed_offer;

    bool family_shared = false;
    bool upgraded      = false;
};

struct Entitlement {
    std::string       product_id;
    SubscriptionState state;
    Transaction       transaction;

    std::optional<TimePoint>        expires_at;
    std::optional<ExpirationReason> expiration_reason;
    bool                            will_auto_renew = false;
    std::optional<std::string>      renewal_product_id;
};

struct PurchaseResult {
    std::string                  product_id;
    std::optional<Transaction>   transaction;
    std::optional<PurchaseError> error;

    bool ok() const { return !error.has_value(); }
};

// ── ProductSpec ───────────────────────────────────────────────────────────────

struct ProductSpec {
    std::string product_id;
    ProductKind kind;
};

// ── OfferSignature ────────────────────────────────────────────────────────────
// Carries both signing shapes so callers don't need OS version guards:
//
//   macOS 26.0+  — populate `jws` with the compact JWS your server returns.
//                  The four legacy fields are ignored on this OS.
//
//   macOS 15.4–25.x — leave `jws` empty and populate the four legacy fields
//                  with the components your server returns.
//
// The Swift layer picks the correct StoreKit API at runtime.

struct OfferSignature {
    // ── macOS 26.0+ ──────────────────────────────────────────────────────────
    std::string jws;               // compact JWS token

    // ── macOS 15.4–25.x ──────────────────────────────────────────────────────
    std::string           key_id;
    std::string           nonce;            // UUID string, e.g. "a1b2c3d4-..."
    int64_t               timestamp  = 0;   // Unix milliseconds
    std::vector<uint8_t>  signature_bytes;  // raw ECDSA bytes, NOT base64
};

// ── Store ─────────────────────────────────────────────────────────────────────

class Store {
public:
     Store();
    ~Store();

    Store(const Store&)            = delete;
    Store& operator=(const Store&) = delete;

    void register_products(std::vector<ProductSpec> specs);

    void fetch_products();

    // Provide offer_id + offer_signature together to apply a promotional offer.
    // Requires macOS 15.4+; silently ignored on earlier OS.
    void purchase(const std::string& product_id,
                  std::optional<std::string>    offer_id        = std::nullopt,
                  std::optional<OfferSignature> offer_signature = std::nullopt);

    void restore_purchases();

    void current_entitlements(std::function<void(std::vector<Entitlement>)> fn);
    void check_entitlement(const std::string& product_id,
                           std::function<void(std::optional<Entitlement>)> fn);

    void request_refund(const std::string& transaction_id,
                        std::function<void(RefundRequestStatus)> fn);

    void on_products_fetched(std::function<void(const std::vector<Product>&)> fn);
    void on_purchase_completed(std::function<void(const PurchaseResult&)> fn);
    void on_restore_completed(std::function<void(const std::vector<PurchaseResult>&)> fn);
    void on_entitlements_changed(std::function<void()> fn);
    void on_promoted_iap(std::function<bool(const std::string& product_id)> fn);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace billing::apple