#include "microsoft_store_manager.hpp"
#if defined(_WIN32)

namespace {

// ── Enum → string ─────────────────────────────────────────────────────────────

std::string product_type_str(billing::microsoft::ProductType t) {
    switch (t) {
        case billing::microsoft::ProductType::App:                 return "app";
        case billing::microsoft::ProductType::Game:                return "game";
        case billing::microsoft::ProductType::Consumable:          return "consumable";
        case billing::microsoft::ProductType::UnmanagedConsumable: return "unmanaged_consumable";
        case billing::microsoft::ProductType::Durable:             return "durable";
    }
    return "unknown";
}

std::string purchase_status_str(billing::microsoft::PurchaseStatus s) {
    switch (s) {
        case billing::microsoft::PurchaseStatus::Succeeded:        return "succeeded";
        case billing::microsoft::PurchaseStatus::AlreadyPurchased: return "already_purchased";
        case billing::microsoft::PurchaseStatus::NotPurchased:     return "not_purchased";
        case billing::microsoft::PurchaseStatus::NetworkError:     return "network_error";
        case billing::microsoft::PurchaseStatus::ServerError:      return "server_error";
        case billing::microsoft::PurchaseStatus::Unknown:          return "unknown";
    }
    return "unknown";
}

std::string consume_status_str(billing::microsoft::ConsumeStatus s) {
    switch (s) {
        case billing::microsoft::ConsumeStatus::Succeeded:            return "succeeded";
        case billing::microsoft::ConsumeStatus::InsufficientQuantity: return "insufficient_quantity";
        case billing::microsoft::ConsumeStatus::NetworkError:         return "network_error";
        case billing::microsoft::ConsumeStatus::ServerError:          return "server_error";
        case billing::microsoft::ConsumeStatus::Unknown:              return "unknown";
    }
    return "unknown";
}

// ── Struct → JSON ─────────────────────────────────────────────────────────────

nlohmann::json product_json(const billing::microsoft::Product& p) {
    return {
        {"id",            p.store_id},
        {"title",         p.title},
        {"description",   p.description},
        {"display_price", p.formatted_price},
        {"kind",          product_type_str(p.type)},
        {"is_owned",      p.is_owned},
    };
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────

namespace arc {

MicrosoftStoreManager::MicrosoftStoreManager(
    std::function<void(nlohmann::json)> emit_fn, HWND owner_hwnd)
    : emit_(std::move(emit_fn))
{
    if (owner_hwnd)
        store_ = billing::microsoft::Store::create(owner_hwnd);
}

// ── Command handlers ──────────────────────────────────────────────────────────

void MicrosoftStoreManager::on_fetch_products(const nlohmann::json& j) {
    if (!store_) return;
    if (!j.contains("product_ids") || !j["product_ids"].is_array()) return;

    std::vector<std::string> ids;
    for (auto& item : j["product_ids"]) {
        if (item.is_string())
            ids.push_back(item.get<std::string>());
    }

    store_->get_products(ids, [this](std::vector<billing::microsoft::Product> products) {
        nlohmann::json arr = nlohmann::json::array();
        for (auto& p : products) arr.push_back(product_json(p));
        emit({ {"type", "microsoft.store.products_fetched"}, {"products", std::move(arr)} });
    });
}

void MicrosoftStoreManager::on_purchase(const nlohmann::json& j) {
    if (!store_) return;
    if (!j.contains("product_id") || !j["product_id"].is_string()) return;
    const std::string product_id = j["product_id"].get<std::string>();

    store_->purchase(product_id,
        [this, product_id](billing::microsoft::PurchaseResult result) {
            emit({
                {"type",       "microsoft.store.purchase_completed"},
                {"product_id", product_id},
                {"status",     purchase_status_str(result.status)},
                {"error",      result.extended_error_message.empty()
                                   ? nlohmann::json(nullptr)
                                   : nlohmann::json(result.extended_error_message)},
            });
        });
}

void MicrosoftStoreManager::on_get_owned() {
    if (!store_) return;

    store_->get_owned_store_ids([this](std::vector<std::string> ids) {
        nlohmann::json arr = nlohmann::json::array();
        for (auto& id : ids) arr.push_back(id);
        emit({ {"type", "microsoft.store.owned"}, {"product_ids", std::move(arr)} });
    });
}

void MicrosoftStoreManager::on_check_entitlement(const nlohmann::json& j) {
    if (!store_) return;
    if (!j.contains("product_id") || !j["product_id"].is_string()) return;
    const std::string product_id = j["product_id"].get<std::string>();

    // Microsoft has no single-product entitlement query; fetch all owned IDs
    // and check membership.  The controller side should not expect a rich
    // entitlement struct here — only the boolean is available.
    store_->get_owned_store_ids([this, product_id](std::vector<std::string> ids) {
        bool owned = false;
        for (auto& id : ids) {
            if (id == product_id) { owned = true; break; }
        }
        emit({
            {"type",       "microsoft.store.entitlement"},
            {"product_id", product_id},
            {"owned",      owned},
        });
    });
}

void MicrosoftStoreManager::on_report_consumable(const nlohmann::json& j) {
    if (!store_) return;
    if (!j.contains("product_id")  || !j["product_id"].is_string())      return;
    if (!j.contains("quantity")    || !j["quantity"].is_number_integer()) return;
    if (!j.contains("tracking_id") || !j["tracking_id"].is_string())     return;

    const std::string product_id  = j["product_id"].get<std::string>();
    const uint32_t    quantity    = j["quantity"].get<uint32_t>();
    const std::string tracking_id = j["tracking_id"].get<std::string>();

    store_->report_consumable_fulfilled(product_id, quantity, tracking_id,
        [this, product_id, tracking_id](billing::microsoft::ConsumeStatus status) {
            emit({
                {"type",        "microsoft.store.consumable_fulfilled"},
                {"product_id",  product_id},
                {"tracking_id", tracking_id},
                {"status",      consume_status_str(status)},
            });
        });
}

// ── Dispatch ──────────────────────────────────────────────────────────────────

void MicrosoftStoreManager::dispatch(const std::string& type, const nlohmann::json& j) {
    if      (type == "microsoft.store.fetch_products")    on_fetch_products(j);
    else if (type == "microsoft.store.purchase")          on_purchase(j);
    else if (type == "microsoft.store.get_owned")         on_get_owned();
    else if (type == "microsoft.store.check_entitlement") on_check_entitlement(j);
    else if (type == "microsoft.store.report_consumable") on_report_consumable(j);
}

void MicrosoftStoreManager::emit(nlohmann::json evt) {
    emit_(std::move(evt));
}

} // namespace arc

#endif // _WIN32