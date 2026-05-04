// src/apple-store/swift/StoreBridge.swift

import Foundation
import StoreKit
import AppKit

// ── Promotional offer signature ───────────────────────────────────────────────
// Mirrors billing::apple::OfferSignature.
// Both signing shapes live here; PurchaseHandler picks the right StoreKit API.
@objc(BillingOfferSignature)
public class BillingOfferSignature: NSObject {
    // macOS 26.0+: compact JWS produced by server
    @objc public let jws: String

    // macOS 15.4–25.x: individual components produced by server
    @objc public let keyID:     String
    @objc public let nonce:     String   // UUID string
    @objc public let timestamp: Int64    // Unix milliseconds
    @objc public let signatureBytes: Data

    @objc public init(jws: String,
               keyID: String, nonce: String, timestamp: Int64, signatureBytes: Data) {
        self.jws            = jws
        self.keyID          = keyID
        self.nonce          = nonce
        self.timestamp      = timestamp
        self.signatureBytes = signatureBytes
    }
}

@available(macOS 12.0, *)
@objc(StoreBridge)
public class StoreBridge: NSObject {

    private let productFetcher     = ProductFetcher()
    private let purchaseHandler    = PurchaseHandler()
    private let entitlementManager = EntitlementManager()
    private let refundHandler      = RefundHandler()

    private var intentListener: AnyObject? = nil

    // MARK: - Registration

    @objc public func registerProductIds(_ ids: [String]) {
        productFetcher.skProductCache = [:]
        _registeredIds = ids
    }
    private var _registeredIds: [String] = []

    // MARK: - Fetch

    @objc public func fetchProducts(completion: @escaping ([BillingProduct], NSError?) -> Void) {
        Task {
            let (products, error) = await productFetcher.fetch(ids: _registeredIds)
            purchaseHandler.skProductCache = productFetcher.skProductCache
            await MainActor.run {
                completion(products, error.map { $0 as NSError })
            }
        }
    }

    // MARK: - Purchase

    @objc(purchase:offerId:offerSignature:completion:)
    public func purchase(productId: String,
                         offerId: String?,
                         offerSignature: BillingOfferSignature?,
                         completion: @escaping (BillingPurchaseResult) -> Void) {
        Task {
            let result = await purchaseHandler.purchase(
                productId:      productId,
                offerId:        offerId,
                offerSignature: offerSignature
            )
            await MainActor.run { completion(result) }
        }
    }

    // MARK: - Restore

    @objc public func restorePurchases(completion: @escaping ([BillingPurchaseResult]) -> Void) {
        Task {
            let results = await entitlementManager.restorePurchases()
            await MainActor.run { completion(results) }
        }
    }

    // MARK: - Entitlements

    @objc public func currentEntitlements(completion: @escaping ([BillingEntitlement]) -> Void) {
        Task {
            let all = await entitlementManager.currentEntitlements()
            await MainActor.run { completion(all) }
        }
    }

    @objc(checkEntitlement:completion:)
    public func checkEntitlement(productId: String,
                                 completion: @escaping (BillingEntitlement?) -> Void) {
        Task {
            let result = await entitlementManager.checkEntitlement(productId: productId)
            await MainActor.run { completion(result) }
        }
    }

    // MARK: - Refund

    @objc(requestRefund:completion:)
    public func requestRefund(transactionId: String,
                              completion: @escaping (BillingRefundStatus) -> Void) {
        Task {
            let status = await refundHandler.requestRefund(transactionId: transactionId)
            await MainActor.run { completion(status) }
        }
    }

    // MARK: - Listeners

    @objc(startEntitlementListenerWithOnChange:)
    public func startEntitlementListener(onChange: @escaping () -> Void) {
        entitlementManager.onEntitlementsChanged = onChange
        entitlementManager.startListening()
    }

    @objc public func startPromotedIAPListener(handler: @escaping (String) -> Bool) {
        if #available(macOS 14.4, *) {
            let listener = PurchaseIntentListener()
            listener.onIntent = handler
            listener.start()
            intentListener = listener
        }
    }
}