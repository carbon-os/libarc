// src/apple-store/swift/ProductFetcher.swift

import StoreKit
import Foundation

@available(macOS 12.0, *)
class ProductFetcher {

    // Fix 6: removed private(set) so StoreBridge can reset the cache in
    // registerProductIds(). The cache is internal plumbing with a single
    // external consumer (StoreBridge), so restricting the setter adds no value.
    var skProductCache: [String: Product] = [:]

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