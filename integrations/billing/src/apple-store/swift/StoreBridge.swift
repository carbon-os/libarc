// src/apple-store/swift/StoreBridge.swift
// The @objc boundary class. The ObjC++ shim holds one instance of this.
// All methods use completion blocks — no async/await leaks across the boundary.
// Internally everything runs in Task {} and dispatches results to MainActor.

import Foundation
import StoreKit
import AppKit

@available(macOS 12.0, *)
@objc(StoreBridge)
class StoreBridge: NSObject {

    private let productFetcher      = ProductFetcher()
    private let purchaseHandler     = PurchaseHandler()
    private let entitlementManager  = EntitlementManager()
    private let refundHandler       = RefundHandler()

    // Conditionally held — only created on macOS 14.4+
    private var intentListener: AnyObject? = nil

    // MARK: - Registration

    @objc func registerProductIds(_ ids: [String]) {
        // Stored on the fetcher; purchase handler cache filled after fetch
        productFetcher.skProductCache = [:]
        _ = ids  // will be used in fetchProducts; pass through
        _registeredIds = ids
    }
    private var _registeredIds: [String] = []

    // MARK: - Fetch

    @objc func fetchProducts(completion: @escaping ([BillingProduct], NSError?) -> Void) {
        Task {
            let (products, error) = await productFetcher.fetch(ids: _registeredIds)
            // Sync the raw SK product cache into the purchase handler
            purchaseHandler.skProductCache = productFetcher.skProductCache
            await MainActor.run {
                completion(products, error.map { $0 as NSError })
            }
        }
    }

    // MARK: - Purchase

    @objc func purchase(productId: String,
                        offerId: String?,
                        jwsSignature: String?,
                        completion: @escaping (BillingPurchaseResult) -> Void) {
        Task {
            let result = await purchaseHandler.purchase(
                productId:    productId,
                offerId:      offerId,
                jwsSignature: jwsSignature
            )
            await MainActor.run { completion(result) }
        }
    }

    // MARK: - Restore

    @objc func restorePurchases(completion: @escaping ([BillingPurchaseResult]) -> Void) {
        Task {
            let results = await entitlementManager.restorePurchases()
            await MainActor.run { completion(results) }
        }
    }

    // MARK: - Entitlements

    @objc func currentEntitlements(completion: @escaping ([BillingEntitlement]) -> Void) {
        Task {
            let all = await entitlementManager.currentEntitlements()
            await MainActor.run { completion(all) }
        }
    }

    @objc func checkEntitlement(productId: String,
                                completion: @escaping (BillingEntitlement?) -> Void) {
        Task {
            let result = await entitlementManager.checkEntitlement(productId: productId)
            await MainActor.run { completion(result) }
        }
    }

    // MARK: - Refund

    @objc func requestRefund(transactionId: String,
                             completion: @escaping (BillingRefundStatus) -> Void) {
        Task {
            let status = await refundHandler.requestRefund(transactionId: transactionId)
            await MainActor.run { completion(status) }
        }
    }

    // MARK: - Listeners

    @objc func startEntitlementListener(onChange: @escaping () -> Void) {
        entitlementManager.onEntitlementsChanged = onChange
        entitlementManager.startListening()
    }

    @objc func startPromotedIAPListener(handler: @escaping (String) -> Bool) {
        if #available(macOS 14.4, *) {
            let listener = PurchaseIntentListener()
            listener.onIntent = handler
            listener.start()
            intentListener = listener
        }
        // No-op on macOS 13 — the handler is registered but will never fire
    }
}