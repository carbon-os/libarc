// src/apple-store/ShimStore.mm

#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>
#import "billing_apple-Swift.h"

#include <billing/apple/store.h>
#include <chrono>
#include <string>
#include <vector>

// ── Impl ──────────────────────────────────────────────────────────────────────

namespace billing::apple {

struct Store::Impl {
    __strong StoreBridge* bridge;

    std::function<void(const std::vector<Product>&)>        on_products_fetched;
    std::function<void(const PurchaseResult&)>              on_purchase_completed;
    std::function<void(const std::vector<PurchaseResult>&)> on_restore_completed;
    std::function<void()>                                   on_entitlements_changed;
    std::function<bool(const std::string&)>                 on_promoted_iap;

    Impl() {
        if (@available(macOS 12.0, *)) {
            bridge = [[StoreBridge alloc] init];
        }
    }
};

} // namespace billing::apple

// ── Converters ────────────────────────────────────────────────────────────────

namespace {

using namespace billing::apple;

std::string to_cpp(NSString* s) {
    return s ? std::string(s.UTF8String) : std::string{};
}

NSString* to_ns(const std::string& s) {
    return [NSString stringWithUTF8String:s.c_str()];
}

TimePoint to_cpp(NSDate* date) {
    if (!date) return TimePoint{};
    auto secs = std::chrono::duration<double>(date.timeIntervalSince1970);
    return TimePoint{ std::chrono::duration_cast<std::chrono::system_clock::duration>(secs) };
}

std::optional<TimePoint> to_cpp_opt(NSDate* date) {
    if (!date) return std::nullopt;
    return to_cpp(date);
}

ProductKind to_cpp(BillingProductKind k) {
    switch (k) {
    case BillingProductKindConsumable:                return ProductKind::Consumable;
    case BillingProductKindNonConsumable:             return ProductKind::NonConsumable;
    case BillingProductKindAutoRenewableSubscription: return ProductKind::AutoRenewableSubscription;
    case BillingProductKindNonRenewingSubscription:   return ProductKind::NonRenewingSubscription;
    }
}

OfferType to_cpp(BillingOfferType t) {
    switch (t) {
    case BillingOfferTypeIntroductory: return OfferType::Introductory;
    case BillingOfferTypePromotional:  return OfferType::Promotional;
    case BillingOfferTypeWinBack:      return OfferType::WinBack;
    case BillingOfferTypeOfferCode:    return OfferType::OfferCode;
    }
}

OfferPaymentMode to_cpp(BillingPaymentMode m) {
    switch (m) {
    case BillingPaymentModeFreeTrial:  return OfferPaymentMode::FreeTrial;
    case BillingPaymentModePayAsYouGo: return OfferPaymentMode::PayAsYouGo;
    case BillingPaymentModePayUpFront: return OfferPaymentMode::PayUpFront;
    }
}

PeriodUnit to_cpp(BillingPeriodUnit u) {
    switch (u) {
    case BillingPeriodUnitDay:   return PeriodUnit::Day;
    case BillingPeriodUnitWeek:  return PeriodUnit::Week;
    case BillingPeriodUnitMonth: return PeriodUnit::Month;
    case BillingPeriodUnitYear:  return PeriodUnit::Year;
    }
}

SubscriptionState to_cpp(BillingSubscriptionState s) {
    switch (s) {
    case BillingSubscriptionStateActive:               return SubscriptionState::Active;
    case BillingSubscriptionStateExpired:              return SubscriptionState::Expired;
    case BillingSubscriptionStateInBillingRetry:       return SubscriptionState::InBillingRetry;
    case BillingSubscriptionStateInBillingGracePeriod: return SubscriptionState::InBillingGracePeriod;
    case BillingSubscriptionStateRevoked:              return SubscriptionState::Revoked;
    }
}

ExpirationReason to_cpp(BillingExpirationReason r) {
    switch (r) {
    case BillingExpirationReasonCancelled:         return ExpirationReason::Cancelled;
    case BillingExpirationReasonBillingError:       return ExpirationReason::BillingError;
    case BillingExpirationReasonPriceIncrease:      return ExpirationReason::PriceIncrease;
    case BillingExpirationReasonProductUnavailable: return ExpirationReason::ProductUnavailable;
    case BillingExpirationReasonUnknown:            return ExpirationReason::Unknown;
    }
}

PurchaseError to_cpp(BillingPurchaseError e) {
    switch (e) {
    case BillingPurchaseErrorCancelled:            return PurchaseError::Cancelled;
    case BillingPurchaseErrorPaymentFailed:        return PurchaseError::PaymentFailed;
    case BillingPurchaseErrorProductNotFound:      return PurchaseError::ProductNotFound;
    case BillingPurchaseErrorNotEntitled:          return PurchaseError::NotEntitled;
    case BillingPurchaseErrorPendingAuthorization: return PurchaseError::PendingAuthorization;
    case BillingPurchaseErrorNetworkError:         return PurchaseError::NetworkError;
    case BillingPurchaseErrorUnknown:              return PurchaseError::Unknown;
    }
}

RefundRequestStatus to_cpp(BillingRefundStatus s) {
    switch (s) {
    case BillingRefundStatusSuccess:       return RefundRequestStatus::Success;
    case BillingRefundStatusUserCancelled: return RefundRequestStatus::UserCancelled;
    case BillingRefundStatusError:         return RefundRequestStatus::Error;
    }
}

SubscriptionPeriod to_cpp(BillingPeriod* p) {
    return { (int)p.value, to_cpp(p.unit) };
}

Offer to_cpp(BillingOffer* o) {
    return {
        to_cpp(o.offerId),
        to_cpp(o.type),
        to_cpp(o.paymentMode),
        to_cpp(o.displayPrice),
        to_cpp(o.period)
    };
}

Product to_cpp(BillingProduct* p) {
    Product cpp;
    cpp.id            = to_cpp(p.productId);
    cpp.title         = to_cpp(p.title);
    cpp.description   = to_cpp(p.productDescription);
    cpp.display_price = to_cpp(p.displayPrice);
    cpp.kind          = to_cpp(p.kind);

    if (p.subscriptionPeriod)
        cpp.subscription_period = to_cpp(p.subscriptionPeriod);
    if (p.introductoryOffer)
        cpp.introductory_offer = to_cpp(p.introductoryOffer);

    for (BillingOffer* o in p.promotionalOffers)
        cpp.promotional_offers.push_back(to_cpp(o));
    for (BillingOffer* o in p.winBackOffers)
        cpp.win_back_offers.push_back(to_cpp(o));

    return cpp;
}

Transaction to_cpp(BillingTransaction* t) {
    Transaction cpp;
    cpp.id            = to_cpp(t.transactionId);
    cpp.original_id   = to_cpp(t.originalId);
    cpp.product_id    = to_cpp(t.productId);
    cpp.product_kind  = to_cpp(t.kind);
    cpp.purchased_at  = to_cpp(t.purchasedAt);
    cpp.expires_at    = to_cpp_opt(t.expiresAt);
    cpp.revoked_at    = to_cpp_opt(t.revokedAt);
    cpp.family_shared = t.familyShared;
    cpp.upgraded      = t.upgraded;
    if (t.redeemedOffer)
        cpp.redeemed_offer = to_cpp(t.redeemedOffer);
    return cpp;
}

Entitlement to_cpp(BillingEntitlement* e) {
    Entitlement cpp;
    cpp.product_id      = to_cpp(e.productId);
    cpp.state           = to_cpp(e.state);
    cpp.transaction     = to_cpp(e.transaction);
    cpp.expires_at      = to_cpp_opt(e.expiresAt);
    cpp.will_auto_renew = e.willAutoRenew;
    if (e.hasExpirationReason)
        cpp.expiration_reason = to_cpp(e.expirationReason);
    if (e.renewalProductId)
        cpp.renewal_product_id = to_cpp(e.renewalProductId);
    return cpp;
}

PurchaseResult to_cpp(BillingPurchaseResult* r) {
    PurchaseResult cpp;
    cpp.product_id = to_cpp(r.productId);
    if (r.succeeded && r.transaction)
        cpp.transaction = to_cpp(r.transaction);
    else
        cpp.error = to_cpp(r.errorCode);
    return cpp;
}

BillingOfferSignature* to_ns(const OfferSignature& sig) {
    return [[BillingOfferSignature alloc]
        initWithJws:         to_ns(sig.jws)
        keyID:               to_ns(sig.key_id)
        nonce:               to_ns(sig.nonce)
        timestamp:           (int64_t)sig.timestamp
        signatureBytes:      [NSData dataWithBytes:sig.signature_bytes.data()
                                            length:sig.signature_bytes.size()]];
}

} // anonymous namespace

// ── Store method implementations ──────────────────────────────────────────────

namespace billing::apple {

Store::Store()  : impl_(std::make_unique<Impl>()) {}
Store::~Store() = default;

void Store::register_products(std::vector<ProductSpec> specs) {
    if (!impl_->bridge) return;
    NSMutableArray<NSString*>* ids = [NSMutableArray array];
    for (auto& s : specs)
        [ids addObject:to_ns(s.product_id)];
    [impl_->bridge registerProductIds:ids];
}

void Store::fetch_products() {
    if (!impl_->bridge) return;
    auto fn = impl_->on_products_fetched;
    [impl_->bridge fetchProductsWithCompletion:^(NSArray<BillingProduct*>* products,
                                                  NSError* __unused err) {
        if (!fn) return;
        std::vector<Product> cpp;
        cpp.reserve(products.count);
        for (BillingProduct* p in products)
            cpp.push_back(to_cpp(p));
        fn(cpp);
    }];
}

void Store::purchase(const std::string& product_id,
                     std::optional<std::string>    offer_id,
                     std::optional<OfferSignature> offer_signature) {
    if (!impl_->bridge) return;
    auto fn = impl_->on_purchase_completed;

    NSString*              oid = offer_id        ? to_ns(*offer_id)        : nil;
    BillingOfferSignature* sig = offer_signature ? to_ns(*offer_signature) : nil;

    [impl_->bridge purchase:to_ns(product_id)
                    offerId:oid
             offerSignature:sig
                 completion:^(BillingPurchaseResult* result) {
        if (fn) fn(to_cpp(result));
    }];
}

void Store::restore_purchases() {
    if (!impl_->bridge) return;
    auto fn = impl_->on_restore_completed;
    [impl_->bridge restorePurchasesWithCompletion:^(NSArray<BillingPurchaseResult*>* results) {
        if (!fn) return;
        std::vector<PurchaseResult> cpp;
        cpp.reserve(results.count);
        for (BillingPurchaseResult* r in results)
            cpp.push_back(to_cpp(r));
        fn(cpp);
    }];
}

void Store::current_entitlements(std::function<void(std::vector<Entitlement>)> fn) {
    if (!impl_->bridge) return;
    [impl_->bridge currentEntitlementsWithCompletion:^(NSArray<BillingEntitlement*>* list) {
        if (!fn) return;
        std::vector<Entitlement> cpp;
        cpp.reserve(list.count);
        for (BillingEntitlement* e in list)
            cpp.push_back(to_cpp(e));
        fn(cpp);
    }];
}

void Store::check_entitlement(const std::string& product_id,
                               std::function<void(std::optional<Entitlement>)> fn) {
    if (!impl_->bridge) return;
    [impl_->bridge checkEntitlement:to_ns(product_id)
                          completion:^(BillingEntitlement* e) {
        if (!fn) return;
        fn(e ? std::optional<Entitlement>(to_cpp(e)) : std::nullopt);
    }];
}

void Store::request_refund(const std::string& transaction_id,
                            std::function<void(RefundRequestStatus)> fn) {
    if (!impl_->bridge) return;
    [impl_->bridge requestRefund:to_ns(transaction_id)
                       completion:^(BillingRefundStatus status) {
        if (fn) fn(to_cpp(status));
    }];
}

void Store::on_products_fetched(std::function<void(const std::vector<Product>&)> fn) {
    impl_->on_products_fetched = std::move(fn);
}

void Store::on_purchase_completed(std::function<void(const PurchaseResult&)> fn) {
    impl_->on_purchase_completed = std::move(fn);
}

void Store::on_restore_completed(std::function<void(const std::vector<PurchaseResult>&)> fn) {
    impl_->on_restore_completed = std::move(fn);
}

void Store::on_entitlements_changed(std::function<void()> fn) {
    impl_->on_entitlements_changed = fn;
    if (!impl_->bridge) return;
    [impl_->bridge startEntitlementListenerWithOnChange:^{ fn(); }];
}

void Store::on_promoted_iap(std::function<bool(const std::string&)> fn) {
    impl_->on_promoted_iap = fn;
    if (!impl_->bridge) return;
    [impl_->bridge startPromotedIAPListenerWithHandler:^BOOL(NSString* productId) {
        return fn ? fn(to_cpp(productId)) : YES;
    }];
}

} // namespace billing::apple