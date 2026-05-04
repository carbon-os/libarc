// src/apple-store/swift/PurchaseHandler.swift

import StoreKit
import Foundation

@available(macOS 12.0, *)
class PurchaseHandler {

    var skProductCache: [String: Product] = [:]

    func purchase(
        productId:      String,
        offerId:        String?,
        offerSignature: BillingOfferSignature?
    ) async -> BillingPurchaseResult {

        guard let product = skProductCache[productId] else {
            return .failure(productId: productId, code: .productNotFound,
                            message: "Product '\(productId)' not in cache — call fetch_products first")
        }

        do {
            var options = Set<Product.PurchaseOption>()

            if let oid = offerId, let sig = offerSignature {
                if #available(macOS 26.0, *) {
                    // promotionalOffer(_:compactJWS:) returns [Product.PurchaseOption],
                    // not a single element. Explicit type required so Swift can resolve
                    // the static method on formUnion's generic Sequence parameter.
                    options.formUnion(Product.PurchaseOption.promotionalOffer(oid, compactJWS: sig.jws))

                } else if let nonceUUID = UUID(uuidString: sig.nonce) {
                    // macOS 15.4–25.x: legacy four-component Signature struct.
                    let skSig = Product.SubscriptionOffer.Signature(
                        keyID:     sig.keyID,
                        nonce:     nonceUUID,
                        timestamp: Int(sig.timestamp),
                        signature: sig.signatureBytes
                    )
                    options.insert(.promotionalOffer(offerID: oid, signature: skSig))
                }
                // Below macOS 15.4: purchase proceeds without the offer applied.
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