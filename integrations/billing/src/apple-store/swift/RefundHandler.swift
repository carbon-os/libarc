// src/apple-store/swift/RefundHandler.swift

import StoreKit
import AppKit
import Foundation

@available(macOS 12.0, *)
class RefundHandler {

    func requestRefund(transactionId: String) async -> BillingRefundStatus {
        guard let txId = UInt64(transactionId) else { return .error }

        // Grab the key window on the main thread before going async
        let window = await MainActor.run { NSApplication.shared.keyWindow }
        guard let window else { return .error }

        do {
            let result = try await Transaction.beginRefundRequest(for: txId, in: window)
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