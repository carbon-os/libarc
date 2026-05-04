// src/apple-store/swift/PurchaseIntentListener.swift
// Promoted IAP — fires when the user taps "Buy" on your App Store product page.
// Requires macOS 14.4+. StoreBridge instantiates this conditionally.

import StoreKit
import Foundation

@available(macOS 14.4, *)
class PurchaseIntentListener {

    // Return true to purchase immediately, false to defer.
    var onIntent: ((String) -> Bool)?
    private var task: Task<Void, Never>?

    func start() {
        task = Task(priority: .background) { [weak self] in
            for await intent in PurchaseIntent.intents {
                let productId = intent.product.id
                // Fix 9: capture a strong reference via guard-let before crossing
                // into MainActor — capturing 'var self' directly is an error in Swift 6.
                guard let self else { return }
                await MainActor.run {
                    // If the caller returns false they're deferring —
                    // they're expected to call purchase() themselves when ready.
                    let _ = self.onIntent?(productId)
                }
            }
        }
    }

    func stop() {
        task?.cancel()
        task = nil
    }

    deinit { stop() }
}