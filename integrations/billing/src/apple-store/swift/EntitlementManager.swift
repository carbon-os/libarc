// src/apple-store/swift/EntitlementManager.swift

import StoreKit
import Foundation

@available(macOS 12.0, *)
class EntitlementManager {

    var onEntitlementsChanged: (() -> Void)?
    private var updatesTask: Task<Void, Never>?

    // MARK: - Listener

    func startListening() {
        updatesTask = Task(priority: .background) { [weak self] in
            // Transaction.updates is an AsyncSequence that emits on renewals,
            // cancellations, billing failures, refunds, Family Share changes, etc.
            for await _ in Transaction.updates {
                await MainActor.run { self?.onEntitlementsChanged?() }
            }
        }
    }

    func stopListening() {
        updatesTask?.cancel()
        updatesTask = nil
    }

    // MARK: - Entitlements

    // SK2 canonical entitlement check — never look at receipt files.
    func currentEntitlements() async -> [BillingEntitlement] {
        var results: [BillingEntitlement] = []

        for await result in Transaction.currentEntitlements {
            guard case .verified(let tx) = result else { continue }

            if tx.productType == .autoRenewable {
                // Full subscription status — includes renewal info, grace period, etc.
                if let statuses = try? await Product.SubscriptionInfo.status(for: tx.productID) {
                    for status in statuses {
                        guard case .verified(let renewalTx) = status.transaction else { continue }
                        results.append(
                            Converters.entitlement(from: status,
                                                   productId: tx.productID,
                                                   latestTx: renewalTx)
                        )
                    }
                }
            } else {
                // Non-consumable / non-renewing subscription — always active if present
                let bridgeTx = Converters.transaction(from: tx)
                results.append(BillingEntitlement(
                    productId:           tx.productID,
                    state:               .active,
                    transaction:         bridgeTx,
                    expiresAt:           tx.expirationDate,
                    hasExpirationReason: false,
                    expirationReason:    .unknown,
                    willAutoRenew:       false,
                    renewalProductId:    nil
                ))
            }
        }

        return results
    }

    func checkEntitlement(productId: String) async -> BillingEntitlement? {
        let all = await currentEntitlements()
        return all.first { $0.productId == productId }
    }

    // Restore in SK2 = re-derive from currentEntitlements.
    // No explicit network call needed — Apple keeps transactions synced.
    func restorePurchases() async -> [BillingPurchaseResult] {
        let entitlements = await currentEntitlements()
        return entitlements.map { e in
            BillingPurchaseResult.success(productId: e.productId,
                                          transaction: e.transaction)
        }
    }

    deinit { stopListening() }
}