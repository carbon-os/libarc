// billing/src/microsoft-store/Store.cpp

#include "billing/microsoft/store.h"

#include <winrt/Windows.Services.Store.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <shobjidl_core.h> // Required for IInitializeWithWindow

using namespace winrt;
using namespace winrt::Windows::Services::Store;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Foundation::Collections;

namespace billing::microsoft {

class StoreImpl : public Store {
private:
    StoreContext context_{ nullptr };
    HWND         hwnd_{ nullptr };

    // ── Coroutine Dispatchers ──────────────────────────────────────────────────

    winrt::fire_and_forget do_get_products(std::vector<std::string> store_ids, 
                                           std::function<void(std::vector<Product>)> callback) 
    {
        try {
            // 1. The SDK requires us to specify which Product Kinds we are querying.
            std::vector<hstring> kinds = {
                L"Application", L"Game", L"Consumable", L"UnmanagedConsumable", L"Durable"
            };
            auto kind_view = winrt::single_threaded_vector<hstring>(std::move(kinds)).GetView();

            // 2. Prepare the Store IDs
            std::vector<hstring> ids;
            for (const auto& id : store_ids) {
                ids.push_back(to_hstring(id));
            }
            auto id_view = winrt::single_threaded_vector<hstring>(std::move(ids)).GetView();

            // 3. Await the async operation
            auto result = co_await context_.GetStoreProductsAsync(kind_view, id_view);

            std::vector<Product> products;
            
            // Check if the HRESULT represents S_OK (0)
            if (result.ExtendedError() == 0) {
                // Products() returns an IMapView<hstring, StoreProduct>
                for (auto const& [key, storeProduct] : result.Products()) {
                    Product p;
                    p.store_id        = to_string(storeProduct.StoreId());
                    p.title           = to_string(storeProduct.Title());
                    p.description     = to_string(storeProduct.Description());
                    p.formatted_price = to_string(storeProduct.Price().FormattedPrice());
                    p.is_owned        = storeProduct.IsInUserCollection();

                    hstring type = storeProduct.ProductKind();
                    if (type == L"Application") p.type = ProductType::App;
                    else if (type == L"Game") p.type = ProductType::Game;
                    else if (type == L"Consumable") p.type = ProductType::Consumable;
                    else if (type == L"UnmanagedConsumable") p.type = ProductType::UnmanagedConsumable;
                    else p.type = ProductType::Durable;

                    products.push_back(std::move(p));
                }
            }
            callback(std::move(products));
        } catch (...) {
            // Catch WinRT exceptions (e.g., no internet connection during request)
            callback({}); 
        }
    }

    winrt::fire_and_forget do_purchase(std::string store_id, std::function<void(PurchaseResult)> callback) 
    {
        PurchaseResult res{ PurchaseStatus::Unknown, "" };
        try {
            auto result = co_await context_.RequestPurchaseAsync(to_hstring(store_id));
            
            switch (result.Status()) {
                case StorePurchaseStatus::Succeeded:        res.status = PurchaseStatus::Succeeded; break;
                case StorePurchaseStatus::AlreadyPurchased: res.status = PurchaseStatus::AlreadyPurchased; break;
                case StorePurchaseStatus::NotPurchased:     res.status = PurchaseStatus::NotPurchased; break;
                case StorePurchaseStatus::NetworkError:     res.status = PurchaseStatus::NetworkError; break;
                case StorePurchaseStatus::ServerError:      res.status = PurchaseStatus::ServerError; break;
                default:                                    res.status = PurchaseStatus::Unknown; break;
            }

            // Capture HRESULT message if it failed
            if (result.ExtendedError() != 0) {
                winrt::hresult_error err(result.ExtendedError());
                res.extended_error_message = to_string(err.message());
            }

        } catch (winrt::hresult_error const& ex) {
            res.extended_error_message = to_string(ex.message());
        }
        callback(std::move(res));
    }

    winrt::fire_and_forget do_report_consumable(std::string store_id, 
                                                uint32_t quantity, 
                                                std::string tracking_id, 
                                                std::function<void(ConsumeStatus)> callback) 
    {
        try {
            // Tracking ID is historically a GUID to ensure idempotency 
            auto result = co_await context_.ReportConsumableFulfillmentAsync(
                to_hstring(store_id), quantity, winrt::guid(tracking_id));

            ConsumeStatus status = ConsumeStatus::Unknown;
            switch (result.Status()) {
                case StoreConsumableStatus::Succeeded:           status = ConsumeStatus::Succeeded; break;
                case StoreConsumableStatus::InsufficentQuantity: status = ConsumeStatus::InsufficientQuantity; break;
                case StoreConsumableStatus::NetworkError:        status = ConsumeStatus::NetworkError; break;
                case StoreConsumableStatus::ServerError:         status = ConsumeStatus::ServerError; break;
                default:                                         status = ConsumeStatus::Unknown; break;
            }
            callback(status);
        } catch (...) {
            callback(ConsumeStatus::Unknown);
        }
    }

    winrt::fire_and_forget do_get_owned(std::function<void(std::vector<std::string>)> callback) 
    {
        try {
            auto license = co_await context_.GetAppLicenseAsync();
            std::vector<std::string> owned;
            
            // AddOnLicenses returns an IMapView<hstring, StoreLicense>
            for (auto const& [key, addOn] : license.AddOnLicenses()) {
                if (addOn.IsActive()) {
                    owned.push_back(to_string(addOn.SkuStoreId())); // SKU ID is preferred for Add-ons
                }
            }
            callback(std::move(owned));
        } catch (...) {
            callback({});
        }
    }

public:
    explicit StoreImpl(HWND hwnd) : hwnd_(hwnd) {
        // Grab the app's store context
        context_ = StoreContext::GetDefault();
        
        // Critical for Desktop Apps: Bind the StoreContext to the main Window HWND
        // so modal dialogs (like the purchase popup) parent correctly.
        auto initWindow = context_.as<IInitializeWithWindow>();
        winrt::check_hresult(initWindow->Initialize(hwnd_));
    }

    void get_products(const std::vector<std::string>& store_ids, 
                      std::function<void(std::vector<Product>)> callback) override {
        do_get_products(store_ids, std::move(callback));
    }

    void purchase(const std::string& store_id, 
                  std::function<void(PurchaseResult)> callback) override {
        do_purchase(store_id, std::move(callback));
    }

    void report_consumable_fulfilled(const std::string& store_id, 
                                     uint32_t quantity, 
                                     const std::string& tracking_id, 
                                     std::function<void(ConsumeStatus)> callback) override {
        do_report_consumable(store_id, quantity, tracking_id, std::move(callback));
    }

    void get_owned_store_ids(std::function<void(std::vector<std::string>)> callback) override {
        do_get_owned(std::move(callback));
    }
};

// ── Factory Method ───────────────────────────────────────────────────────────

std::unique_ptr<Store> Store::create(HWND owner_hwnd) {
    return std::make_unique<StoreImpl>(owner_hwnd);
}

} // namespace billing::microsoft