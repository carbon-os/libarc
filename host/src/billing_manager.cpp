#include "billing_manager.hpp"
#if defined(__APPLE__) || defined(_WIN32)

#include "main_thread.hpp" // post_to_main_thread — used in async callbacks

#include <chrono>
#include <cstdint>
#include <string>

// ── Minimal base64 decoder ────────────────────────────────────────────────────
// Used to decode the offer signature bytes sent as base64 by the controller.
namespace {

static const std::string kB64Chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::vector<uint8_t> base64_decode(const std::string& in) {
    std::vector<uint8_t> out;
    out.reserve(in.size() * 3 / 4);
    int val = 0, valb = -8;
    for (unsigned char c : in) {
        if (c == '=') break;
        auto pos = kB64Chars.find(static_cast<char>(c));
        if (pos == std::string::npos) continue;
        val = (val << 6) | static_cast<int>(pos);
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

// ── Shared serialisation helpers ─────────────────────────────────────────────

int64_t to_unix_ms(std::chrono::system_clock::time_point tp) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        tp.time_since_epoch()).count();
}

nlohmann::json maybe_ms(const std::optional<std::chrono::system_clock::time_point>& opt) {
    if (!opt) return nullptr;
    return to_unix_ms(*opt);
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
//  Apple implementation
// ─────────────────────────────────────────────────────────────────────────────
#if defined(__APPLE__)

namespace {

// ── Enum → string ─────────────────────────────────────────────────────────────

std::string kind_str(billing::apple::ProductKind k) {
    switch (k) {
        case billing::apple::ProductKind::Consumable:                return "consumable";
        case billing::apple::ProductKind::NonConsumable:             return "non_consumable";
        case billing::apple::ProductKind::AutoRenewableSubscription: return "auto_renewable_subscription";
        case billing::apple::ProductKind::NonRenewingSubscription:   return "non_renewing_subscription";
    }
    return "unknown";
}

std::string period_unit_str(billing::apple::PeriodUnit u) {
    switch (u) {
        case billing::apple::PeriodUnit::Day:   return "day";
        case billing::apple::PeriodUnit::Week:  return "week";
        case billing::apple::PeriodUnit::Month: return "month";
        case billing::apple::PeriodUnit::Year:  return "year";
    }
    return "unknown";
}

std::string offer_type_str(billing::apple::OfferType t) {
    switch (t) {
        case billing::apple::OfferType::Introductory:  return "introductory";
        case billing::apple::OfferType::Promotional:   return "promotional";
        case billing::apple::OfferType::WinBack:       return "win_back";
        case billing::apple::OfferType::OfferCode:     return "offer_code";
    }
    return "unknown";
}

std::string payment_mode_str(billing::apple::OfferPaymentMode m) {
    switch (m) {
        case billing::apple::OfferPaymentMode::FreeTrial:   return "free_trial";
        case billing::apple::OfferPaymentMode::PayAsYouGo:  return "pay_as_you_go";
        case billing::apple::OfferPaymentMode::PayUpFront:  return "pay_up_front";
    }
    return "unknown";
}

std::string sub_state_str(billing::apple::SubscriptionState s) {
    switch (s) {
        case billing::apple::SubscriptionState::Active:               return "active";
        case billing::apple::SubscriptionState::Expired:              return "expired";
        case billing::apple::SubscriptionState::InBillingRetry:       return "in_billing_retry";
        case billing::apple::SubscriptionState::InBillingGracePeriod: return "in_billing_grace_period";
        case billing::apple::SubscriptionState::Revoked:              return "revoked";
    }
    return "unknown";
}

std::string expiration_reason_str(billing::apple::ExpirationReason r) {
    switch (r) {
        case billing::apple::ExpirationReason::Cancelled:          return "cancelled";
        case billing::apple::ExpirationReason::BillingError:       return "billing_error";
        case billing::apple::ExpirationReason::PriceIncrease:      return "price_increase";
        case billing::apple::ExpirationReason::ProductUnavailable: return "product_unavailable";
        case billing::apple::ExpirationReason::Unknown:            return "unknown";
    }
    return "unknown";
}

std::string purchase_error_str(billing::apple::PurchaseError e) {
    switch (e) {
        case billing::apple::PurchaseError::Cancelled:            return "cancelled";
        case billing::apple::PurchaseError::PaymentFailed:        return "payment_failed";
        case billing::apple::PurchaseError::ProductNotFound:      return "product_not_found";
        case billing::apple::PurchaseError::NotEntitled:          return "not_entitled";
        case billing::apple::PurchaseError::PendingAuthorization: return "pending_authorization";
        case billing::apple::PurchaseError::NetworkError:         return "network_error";
        case billing::apple::PurchaseError::Unknown:              return "unknown";
    }
    return "unknown";
}

// ── string → enum ─────────────────────────────────────────────────────────────

billing::apple::ProductKind kind_from_str(const std::string& s) {
    if (s == "consumable")                   return billing::apple::ProductKind::Consumable;
    if (s == "non_consumable")               return billing::apple::ProductKind::NonConsumable;
    if (s == "auto_renewable_subscription")  return billing::apple::ProductKind::AutoRenewableSubscription;
    if (s == "non_renewing_subscription")    return billing::apple::ProductKind::NonRenewingSubscription;
    return billing::apple::ProductKind::NonConsumable; // safe default
}

// ── Struct → JSON ─────────────────────────────────────────────────────────────

nlohmann::json period_json(const billing::apple::SubscriptionPeriod& p) {
    return { {"value", p.value}, {"unit", period_unit_str(p.unit)} };
}

nlohmann::json offer_json(const billing::apple::Offer& o) {
    return {
        {"id",            o.id},
        {"type",          offer_type_str(o.type)},
        {"payment_mode",  payment_mode_str(o.payment_mode)},
        {"display_price", o.display_price},
        {"period",        period_json(o.period)},
    };
}

nlohmann::json product_json(const billing::apple::Product& p) {
    nlohmann::json j = {
        {"id",            p.id},
        {"title",         p.title},
        {"description",   p.description},
        {"display_price", p.display_price},
        {"kind",          kind_str(p.kind)},
    };

    if (p.subscription_period)
        j["subscription_period"] = period_json(*p.subscription_period);
    else
        j["subscription_period"] = nullptr;

    if (p.introductory_offer)
        j["introductory_offer"] = offer_json(*p.introductory_offer);
    else
        j["introductory_offer"] = nullptr;

    nlohmann::json promo = nlohmann::json::array();
    for (auto& o : p.promotional_offers) promo.push_back(offer_json(o));
    j["promotional_offers"] = std::move(promo);

    nlohmann::json wb = nlohmann::json::array();
    for (auto& o : p.win_back_offers) wb.push_back(offer_json(o));
    j["win_back_offers"] = std::move(wb);

    return j;
}

nlohmann::json transaction_json(const billing::apple::Transaction& t) {
    nlohmann::json j = {
        {"id",            t.id},
        {"original_id",   t.original_id},
        {"product_id",    t.product_id},
        {"product_kind",  kind_str(t.product_kind)},
        {"purchased_at",  to_unix_ms(t.purchased_at)},
        {"expires_at",    maybe_ms(t.expires_at)},
        {"revoked_at",    maybe_ms(t.revoked_at)},
        {"family_shared", t.family_shared},
        {"upgraded",      t.upgraded},
    };
    if (t.redeemed_offer)
        j["redeemed_offer"] = offer_json(*t.redeemed_offer);
    else
        j["redeemed_offer"] = nullptr;
    return j;
}

nlohmann::json entitlement_json(const billing::apple::Entitlement& e) {
    nlohmann::json j = {
        {"product_id",         e.product_id},
        {"state",              sub_state_str(e.state)},
        {"transaction",        transaction_json(e.transaction)},
        {"expires_at",         maybe_ms(e.expires_at)},
        {"will_auto_renew",    e.will_auto_renew},
        {"renewal_product_id", e.renewal_product_id ? nlohmann::json(*e.renewal_product_id)
                                                     : nlohmann::json(nullptr)},
    };
    if (e.expiration_reason)
        j["expiration_reason"] = expiration_reason_str(*e.expiration_reason);
    else
        j["expiration_reason"] = nullptr;
    return j;
}

// ── JSON → OfferSignature ─────────────────────────────────────────────────────

billing::apple::OfferSignature sig_from_json(const nlohmann::json& j) {
    billing::apple::OfferSignature sig;
    if (j.contains("jws") && j["jws"].is_string())
        sig.jws = j["jws"].get<std::string>();
    if (j.contains("key_id") && j["key_id"].is_string())
        sig.key_id = j["key_id"].get<std::string>();
    if (j.contains("nonce") && j["nonce"].is_string())
        sig.nonce = j["nonce"].get<std::string>();
    if (j.contains("timestamp") && j["timestamp"].is_number_integer())
        sig.timestamp = j["timestamp"].get<int64_t>();
    if (j.contains("signature_b64") && j["signature_b64"].is_string())
        sig.signature_bytes = base64_decode(j["signature_b64"].get<std::string>());
    return sig;
}

} // namespace

namespace arc {

BillingManager::BillingManager(std::function<void(nlohmann::json)> emit_fn)
    : emit_(std::move(emit_fn))
{
    // ── Wire async event callbacks ─────────────────────────────────────────────
    // Billing callbacks may arrive on any thread; emit_() (→ ipc::Server::send)
    // is thread-safe so no extra marshalling is needed here.

    store_.on_products_fetched([this](const std::vector<billing::apple::Product>& products) {
        nlohmann::json arr = nlohmann::json::array();
        for (auto& p : products) arr.push_back(product_json(p));
        emit({ {"type", "billing.products_fetched"}, {"products", std::move(arr)} });
    });

    store_.on_purchase_completed([this](const billing::apple::PurchaseResult& r) {
        nlohmann::json j = {
            {"type",       "billing.purchase_completed"},
            {"product_id", r.product_id},
        };
        if (r.transaction)
            j["transaction"] = transaction_json(*r.transaction);
        else
            j["transaction"] = nullptr;
        if (r.error)
            j["error"] = purchase_error_str(*r.error);
        else
            j["error"] = nullptr;
        emit(std::move(j));
    });

    store_.on_restore_completed([this](const std::vector<billing::apple::PurchaseResult>& results) {
        nlohmann::json arr = nlohmann::json::array();
        for (auto& r : results) {
            nlohmann::json item = { {"product_id", r.product_id} };
            if (r.transaction) item["transaction"] = transaction_json(*r.transaction);
            else               item["transaction"] = nullptr;
            if (r.error)       item["error"] = purchase_error_str(*r.error);
            else               item["error"] = nullptr;
            arr.push_back(std::move(item));
        }
        emit({ {"type", "billing.restore_completed"}, {"results", std::move(arr)} });
    });

    store_.on_entitlements_changed([this]() {
        emit({ {"type", "billing.entitlements_changed"} });
    });

    // When the user taps "Buy" from the App Store page we emit the event and
    // return false (defer) so the controller can decide whether to call
    // billing.purchase itself (e.g. after showing its own UI or checking
    // eligibility).
    store_.on_promoted_iap([this](const std::string& product_id) -> bool {
        emit({ {"type", "billing.promoted_iap"}, {"product_id", product_id} });
        return false; // defer — controller drives the purchase
    });
}

// ── Command handlers ──────────────────────────────────────────────────────────

void BillingManager::on_fetch_products(const nlohmann::json& j) {
    // Expected: { "products": [ { "id": "...", "kind": "..." }, ... ] }
    if (!j.contains("products") || !j["products"].is_array()) return;

    std::vector<billing::apple::ProductSpec> specs;
    for (auto& item : j["products"]) {
        if (!item.contains("id") || !item["id"].is_string()) continue;
        billing::apple::ProductSpec s;
        s.product_id = item["id"].get<std::string>();
        s.kind       = kind_from_str(item.value("kind", "non_consumable"));
        specs.push_back(std::move(s));
    }

    store_.register_products(std::move(specs));
    store_.fetch_products();
}

void BillingManager::on_purchase(const nlohmann::json& j) {
    if (!j.contains("product_id") || !j["product_id"].is_string()) return;
    const std::string product_id = j["product_id"].get<std::string>();

    std::optional<std::string>                   offer_id;
    std::optional<billing::apple::OfferSignature> offer_sig;

    if (j.contains("offer_id") && j["offer_id"].is_string())
        offer_id = j["offer_id"].get<std::string>();

    if (j.contains("offer_signature") && j["offer_signature"].is_object())
        offer_sig = sig_from_json(j["offer_signature"]);

    store_.purchase(product_id, std::move(offer_id), std::move(offer_sig));
}

void BillingManager::on_restore_purchases() {
    store_.restore_purchases();
}

void BillingManager::on_current_entitlements() {
    store_.current_entitlements([this](std::vector<billing::apple::Entitlement> ents) {
        nlohmann::json arr = nlohmann::json::array();
        for (auto& e : ents) arr.push_back(entitlement_json(e));
        emit({ {"type", "billing.entitlements"}, {"entitlements", std::move(arr)} });
    });
}

void BillingManager::on_check_entitlement(const nlohmann::json& j) {
    if (!j.contains("product_id") || !j["product_id"].is_string()) return;
    const std::string product_id = j["product_id"].get<std::string>();

    store_.check_entitlement(product_id,
        [this, product_id](std::optional<billing::apple::Entitlement> ent) {
            nlohmann::json reply = {
                {"type",       "billing.entitlement"},
                {"product_id", product_id},
                {"entitlement", ent ? nlohmann::json(entitlement_json(*ent))
                                    : nlohmann::json(nullptr)},
            };
            emit(std::move(reply));
        });
}

void BillingManager::on_request_refund(const nlohmann::json& j) {
    if (!j.contains("transaction_id") || !j["transaction_id"].is_string()) return;
    const std::string txn_id = j["transaction_id"].get<std::string>();

    store_.request_refund(txn_id,
        [this, txn_id](billing::apple::RefundRequestStatus status) {
            std::string s;
            switch (status) {
                case billing::apple::RefundRequestStatus::Success:       s = "success";        break;
                case billing::apple::RefundRequestStatus::UserCancelled: s = "user_cancelled"; break;
                case billing::apple::RefundRequestStatus::Error:         s = "error";          break;
            }
            emit({ {"type", "billing.refund_status"},
                   {"transaction_id", txn_id},
                   {"status", s} });
        });
}

} // namespace arc

// ─────────────────────────────────────────────────────────────────────────────
//  Windows implementation
// ─────────────────────────────────────────────────────────────────────────────
#elif defined(_WIN32)

namespace {

std::string product_type_str(billing::microsoft::ProductType t) {
    switch (t) {
        case billing::microsoft::ProductType::App:                  return "app";
        case billing::microsoft::ProductType::Game:                 return "game";
        case billing::microsoft::ProductType::Consumable:           return "consumable";
        case billing::microsoft::ProductType::UnmanagedConsumable:  return "unmanaged_consumable";
        case billing::microsoft::ProductType::Durable:              return "durable";
    }
    return "unknown";
}

std::string purchase_status_str(billing::microsoft::PurchaseStatus s) {
    switch (s) {
        case billing::microsoft::PurchaseStatus::Succeeded:         return "succeeded";
        case billing::microsoft::PurchaseStatus::AlreadyPurchased:  return "already_purchased";
        case billing::microsoft::PurchaseStatus::NotPurchased:      return "not_purchased";
        case billing::microsoft::PurchaseStatus::NetworkError:      return "network_error";
        case billing::microsoft::PurchaseStatus::ServerError:       return "server_error";
        case billing::microsoft::PurchaseStatus::Unknown:           return "unknown";
    }
    return "unknown";
}

nlohmann::json product_json(const billing::microsoft::Product& p) {
    return {
        {"id",              p.store_id},
        {"title",           p.title},
        {"description",     p.description},
        {"display_price",   p.formatted_price},
        {"kind",            product_type_str(p.type)},
        {"is_owned",        p.is_owned},
    };
}

} // namespace

namespace arc {

BillingManager::BillingManager(std::function<void(nlohmann::json)> emit_fn, HWND owner_hwnd)
    : emit_(std::move(emit_fn))
{
    if (owner_hwnd)
        store_ = billing::microsoft::Store::create(owner_hwnd);
    // If owner_hwnd is null (e.g. very early in startup) store_ stays null and
    // every command handler below is a guarded no-op.
}

void BillingManager::on_fetch_products(const nlohmann::json& j) {
    if (!store_) return;
    if (!j.contains("products") || !j["products"].is_array()) return;

    std::vector<std::string> ids;
    for (auto& item : j["products"]) {
        if (item.contains("id") && item["id"].is_string())
            ids.push_back(item["id"].get<std::string>());
    }

    store_->get_products(ids, [this](std::vector<billing::microsoft::Product> products) {
        nlohmann::json arr = nlohmann::json::array();
        for (auto& p : products) arr.push_back(product_json(p));
        emit({ {"type", "billing.products_fetched"}, {"products", std::move(arr)} });
    });
}

void BillingManager::on_purchase(const nlohmann::json& j) {
    if (!store_) return;
    if (!j.contains("product_id") || !j["product_id"].is_string()) return;
    const std::string product_id = j["product_id"].get<std::string>();

    store_->purchase(product_id,
        [this, product_id](billing::microsoft::PurchaseResult result) {
            nlohmann::json reply = {
                {"type",       "billing.purchase_completed"},
                {"product_id", product_id},
                {"status",     purchase_status_str(result.status)},
            };
            if (!result.extended_error_message.empty())
                reply["error"] = result.extended_error_message;
            else
                reply["error"] = nullptr;
            emit(std::move(reply));
        });
}

void BillingManager::on_restore_purchases() {
    if (!store_) return;
    store_->get_owned_store_ids([this](std::vector<std::string> ids) {
        nlohmann::json arr = nlohmann::json::array();
        for (auto& id : ids) arr.push_back(id);
        emit({ {"type", "billing.restore_completed"}, {"owned_ids", std::move(arr)} });
    });
}

// Windows Store has no "current_entitlements" concept distinct from
// get_owned_store_ids, so alias it to the same response shape.
void BillingManager::on_current_entitlements() {
    on_restore_purchases();
}

// Windows Store has no per-product entitlement query; check ownership via the
// full product list and filter client-side.  We re-use get_owned_store_ids and
// check membership.
void BillingManager::on_check_entitlement(const nlohmann::json& j) {
    if (!store_) return;
    if (!j.contains("product_id") || !j["product_id"].is_string()) return;
    const std::string product_id = j["product_id"].get<std::string>();

    store_->get_owned_store_ids([this, product_id](std::vector<std::string> ids) {
        bool owned = false;
        for (auto& id : ids) {
            if (id == product_id) { owned = true; break; }
        }
        emit({ {"type",       "billing.entitlement"},
               {"product_id", product_id},
               {"owned",      owned},
               // Structured Entitlement not available on Windows; signal this
               // to the controller so it knows to use the "owned" bool only.
               {"entitlement", nullptr} });
    });
}

// Windows has no programmatic refund API; surface this as an unsupported event
// so the controller can handle it gracefully (e.g. open the Store refund page).
void BillingManager::on_request_refund(const nlohmann::json& j) {
    const std::string txn_id = j.value("transaction_id", "");
    emit({ {"type",           "billing.refund_status"},
           {"transaction_id", txn_id},
           {"status",         "unsupported"} });
}

} // namespace arc

#endif // _WIN32

// ─────────────────────────────────────────────────────────────────────────────
//  Shared dispatch + emit (compiled for both platforms)
// ─────────────────────────────────────────────────────────────────────────────
namespace arc {

void BillingManager::dispatch(const std::string& type, const nlohmann::json& j) {
    if      (type == "billing.fetch_products")     on_fetch_products(j);
    else if (type == "billing.purchase")           on_purchase(j);
    else if (type == "billing.restore_purchases")  on_restore_purchases();
    else if (type == "billing.current_entitlements") on_current_entitlements();
    else if (type == "billing.check_entitlement")  on_check_entitlement(j);
    else if (type == "billing.request_refund")     on_request_refund(j);
}

void BillingManager::emit(nlohmann::json evt) {
    emit_(std::move(evt));
}

} // namespace arc

#endif // __APPLE__ || _WIN32