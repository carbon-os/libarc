// src/apple-store/swift/ProductFetcher.swift

import StoreKit
import Foundation

@available(macOS 12.0, *)
class ProductFetcher {

    // Raw SK products kept alive so PurchaseHandler can call product.purchase()
    private(set) var skProductCache: [String: Product] = [:]

    func fetch(ids: [String]) async -> (products: [BillingProduct], error: Error?) {
        do {
            let skProducts = try await Product.products(for: ids)

            // Refresh cache for purchase calls
            skProductCache = Dictionary(uniqueKeysWithValues: skProducts.map { ($0.id, $0) })

            let bridgeProducts = skProducts.map { Converters.product(from: $0) }
            return (bridgeProducts, nil)
        } catch {
            return ([], error)
        }
    }
}