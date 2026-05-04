// src/apple-store/swift/RefundHandler.swift

import StoreKit
import AppKit
import Foundation

@available(macOS 12.0, *)
class RefundHandler {

    func requestRefund(transactionId: String) async -> BillingRefundStatus {
        guard let txId = UInt64(transactionId) else { return .error }

        // Fix 5: Transaction.beginRefundRequest now takes NSViewController, not NSWindow.
        // Grab the content view controller of the key window on the main thread.
        let viewController = await MainActor.run {
            NSApplication.shared.keyWindow?.contentViewController
        }
        guard let viewController else { return .error }

        do {
            let result = try await Transaction.beginRefundRequest(for: txId, in: viewController)
            switch result {
            case .success:       return .success
            case .userCancelled: return .userCancelled
            @unknown default:    return .error
            }
        } catch {
            return .error
        }
    }
}