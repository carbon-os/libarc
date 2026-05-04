// src/apple-store/swift/PurchaseHandler.swift

import StoreKit
import Foundation

@available(macOS 12.0, *)
class PurchaseHandler {

    // Populated by ProductFetcher after a successful fetch
    var skProductCache: [String: Product] = [:]

    func purchase(
        productId: String,
        offerId: String?,
        jwsSignature: String?
    ) async -> BillingPurchaseResult {

        guard let product = skProductCache[productId] else {
            return .failure(productId: productId, code: .productNotFound,
                            message: "Product '\(productId)' not in cache — call fetch_products first")
        }

        do {
            var options = Set<Product.PurchaseOption>()

            // Promotional offer with compact JWS — requires macOS 15.4+.
            // On older OS the purchase proceeds without the offer applied.
            if #available(macOS 15.4, *), let oid = offerId, let jws = jwsSignature {
                options.insert(.promotionalOffer(offerID: oid, signature: jws))
            }

            let result = try await product.purchase(options: options)

            switch result {
            case .success(let verification):
                switch verification {
                case .verified(let tx):
                    await tx.finish()
                    return .success(productId: productId,
                                    transaction: Converters.transaction(from: tx))
                case .unverified(_, let err):
                    return .failure(productId: productId, code: .notEntitled,
                                    message: err.localizedDescription)
                }

            case .userCancelled:
                return .failure(productId: productId, code: .cancelled)

            case .pending:
                return .failure(productId: productId, code: .pendingAuthorization,
                                message: "Waiting for Ask to Buy approval")

            @unknown default:
                return .failure(productId: productId, code: .unknown)
            }

        } catch StoreKitError.networkError(let underlying) {
            return .failure(productId: productId, code: .networkError,
                            message: underlying.localizedDescription)
        } catch {
            return .failure(productId: productId, code: .paymentFailed,
                            message: error.localizedDescription)
        }
    }
}