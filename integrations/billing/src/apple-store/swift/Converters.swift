// src/apple-store/swift/Converters.swift
// One-way: SK2 types → BillingXxx bridge types.
// Nothing in here is @objc — it's internal plumbing.

import Foundation
import StoreKit

@available(macOS 12.0, *)
enum Converters {

    // MARK: - Product

    static func product(from sk: Product) -> BillingProduct {
        var subPeriod:    BillingPeriod? = nil
        var introOffer:   BillingOffer?  = nil
        var promoOffers:  [BillingOffer] = []
        var winBackOffers:[BillingOffer] = []

        if let sub = sk.subscription {
            subPeriod   = period(from: sub.subscriptionPeriod)
            introOffer  = sub.introductoryOffer.map { offer(from: $0, as: .introductory) }
            promoOffers = sub.promotionalOffers.map  { offer(from: $0, as: .promotional) }
            if #available(macOS 14.0, *) {
                winBackOffers = sub.winBackOffers.map { offer(from: $0, as: .winBack) }
            }
        }

        return BillingProduct(
            productId:          sk.id,
            title:              sk.displayName,
            productDescription: sk.description,
            displayPrice:       sk.displayPrice,
            kind:               productKind(from: sk.type),
            subscriptionPeriod: subPeriod,
            introductoryOffer:  introOffer,
            promotionalOffers:  promoOffers,
            winBackOffers:      winBackOffers
        )
    }

    // MARK: - Transaction

    static func transaction(from tx: Transaction) -> BillingTransaction {
        var redeemedOffer: BillingOffer? = nil
        if #available(macOS 14.4, *), let txOffer = tx.offer {
            redeemedOffer = offer(fromTxOffer: txOffer)
        }
        return BillingTransaction(
            transactionId: String(tx.id),
            originalId:    String(tx.originalID),
            productId:     tx.productID,
            kind:          productKind(from: tx.productType),
            purchasedAt:   tx.purchaseDate,
            expiresAt:     tx.expirationDate,
            revokedAt:     tx.revocationDate,
            redeemedOffer: redeemedOffer,
            familyShared:  tx.ownershipType == .familyShared,
            upgraded:      tx.isUpgraded
        )
    }

    // MARK: - Entitlement

    static func entitlement(
        from status: Product.SubscriptionInfo.Status,
        productId: String,
        latestTx: Transaction
    ) -> BillingEntitlement {
        var willAutoRenew  = false
        var renewProductId: String? = nil
        var hasExpReason   = false
        var expReason: BillingExpirationReason = .unknown

        if case .verified(let renewal) = status.renewalInfo {
            willAutoRenew = renewal.willAutoRenew
            if renewal.autoRenewProductID != latestTx.productID {
                renewProductId = renewal.autoRenewProductID
            }
            if let r = renewal.expirationReason {
                hasExpReason = true
                expReason    = expirationReason(from: r)
            }
        }

        return BillingEntitlement(
            productId:           productId,
            state:               subscriptionState(from: status.state),
            transaction:         transaction(from: latestTx),
            expiresAt:           latestTx.expirationDate,
            hasExpirationReason: hasExpReason,
            expirationReason:    expReason,
            willAutoRenew:       willAutoRenew,
            renewalProductId:    renewProductId
        )
    }

    // MARK: - Primitive mappings

    static func productKind(from type: Product.ProductType) -> BillingProductKind {
        switch type {
        case .consumable:    return .consumable
        case .nonConsumable: return .nonConsumable
        case .autoRenewable: return .autoRenewableSubscription
        case .nonRenewable:  return .nonRenewingSubscription
        default:             return .nonConsumable
        }
    }

    static func period(from sk: Product.SubscriptionPeriod) -> BillingPeriod {
        let unit: BillingPeriodUnit
        switch sk.unit {
        case .day:    unit = .day
        case .week:   unit = .week
        case .month:  unit = .month
        case .year:   unit = .year
        @unknown default: unit = .month
        }
        return BillingPeriod(value: sk.value, unit: unit)
    }

    static func offer(from sk: Product.SubscriptionOffer, as type: BillingOfferType) -> BillingOffer {
        let mode: BillingPaymentMode
        switch sk.paymentMode {
        case .freeTrial:  mode = .freeTrial
        case .payAsYouGo: mode = .payAsYouGo
        case .payUpFront: mode = .payUpFront
        @unknown default: mode = .freeTrial
        }
        return BillingOffer(
            offerId:      sk.id ?? "",
            type:         type,
            paymentMode:  mode,
            displayPrice: sk.displayPrice,
            period:       period(from: sk.period)
        )
    }

    @available(macOS 14.4, *)
    static func offer(fromTxOffer tx: Transaction.Offer) -> BillingOffer {
        let type: BillingOfferType
        switch tx.type {
        case .introductoryOffer: type = .introductory
        case .promotionalOffer:  type = .promotional
        case .winBackOffer:      type = .winBack
        @unknown default:        type = .promotional
        }
        let mode: BillingPaymentMode
        switch tx.paymentMode {
        case .freeTrial:  mode = .freeTrial
        case .payAsYouGo: mode = .payAsYouGo
        case .payUpFront: mode = .payUpFront
        default:          mode = .freeTrial
        }
        // Transaction.Offer doesn't carry display price or period —
        // those live on Product.SubscriptionOffer at product-fetch time.
        return BillingOffer(
            offerId:      tx.id ?? "",
            type:         type,
            paymentMode:  mode,
            displayPrice: "",
            period:       BillingPeriod(value: 0, unit: .month)
        )
    }

    static func subscriptionState(
        from state: Product.SubscriptionInfo.RenewalState
    ) -> BillingSubscriptionState {
        switch state {
        case .subscribed:            return .active
        case .expired:               return .expired
        case .inBillingRetryPeriod:  return .inBillingRetry
        case .inGracePeriod:         return .inBillingGracePeriod
        case .revoked:               return .revoked
        @unknown default:            return .expired
        }
    }

    static func expirationReason(
        from reason: Product.SubscriptionInfo.RenewalInfo.ExpirationReason
    ) -> BillingExpirationReason {
        switch reason {
        case .autoRenewDisabled:             return .cancelled
        case .billingError:                  return .billingError
        case .didNotConsentToPriceIncrease:  return .priceIncrease
        case .productUnavailable:            return .productUnavailable
        @unknown default:                    return .unknown
        }
    }
}