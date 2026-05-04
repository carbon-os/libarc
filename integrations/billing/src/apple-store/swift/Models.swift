// src/apple-store/swift/Models.swift
// @objc bridge types — the vocabulary the shim speaks.
// Only ObjC-compatible types at the boundary: NSObject subclasses,
// NSString, NSArray, NSDate, BOOL, Int, and @objc enums.

import Foundation
import StoreKit

// MARK: - Enums

@objc enum BillingProductKind: Int {
    case consumable = 0
    case nonConsumable = 1
    case autoRenewableSubscription = 2
    case nonRenewingSubscription = 3
}

@objc enum BillingOfferType: Int {
    case introductory = 0
    case promotional  = 1
    case winBack      = 2
    case offerCode    = 3
}

@objc enum BillingPaymentMode: Int {
    case freeTrial  = 0
    case payAsYouGo = 1
    case payUpFront = 2
}

@objc enum BillingPeriodUnit: Int {
    case day   = 0
    case week  = 1
    case month = 2
    case year  = 3
}

@objc enum BillingSubscriptionState: Int {
    case active              = 0
    case expired             = 1
    case inBillingRetry      = 2
    case inBillingGracePeriod = 3
    case revoked             = 4
}

@objc enum BillingExpirationReason: Int {
    case cancelled          = 0
    case billingError       = 1
    case priceIncrease      = 2
    case productUnavailable = 3
    case unknown            = 4
}

@objc enum BillingPurchaseError: Int {
    case cancelled            = 0
    case paymentFailed        = 1
    case productNotFound      = 2
    case notEntitled          = 3
    case pendingAuthorization = 4
    case networkError         = 5
    case unknown              = 6
}

@objc enum BillingRefundStatus: Int {
    case success       = 0
    case userCancelled = 1
    case error         = 2
}

// MARK: - Value objects

@objc(BillingPeriod)
class BillingPeriod: NSObject {
    @objc let value: Int
    @objc let unit:  BillingPeriodUnit
    @objc init(value: Int, unit: BillingPeriodUnit) {
        self.value = value; self.unit = unit
    }
}

@objc(BillingOffer)
class BillingOffer: NSObject {
    @objc let offerId:      String
    @objc let type:         BillingOfferType
    @objc let paymentMode:  BillingPaymentMode
    @objc let displayPrice: String
    @objc let period:       BillingPeriod
    @objc init(offerId: String, type: BillingOfferType, paymentMode: BillingPaymentMode,
               displayPrice: String, period: BillingPeriod) {
        self.offerId = offerId; self.type = type; self.paymentMode = paymentMode
        self.displayPrice = displayPrice; self.period = period
    }
}

@objc(BillingProduct)
class BillingProduct: NSObject {
    @objc let productId:          String
    @objc let title:              String
    @objc let productDescription: String
    @objc let displayPrice:       String
    @objc let kind:               BillingProductKind
    @objc let subscriptionPeriod: BillingPeriod?    // nil for non-subscriptions
    @objc let introductoryOffer:  BillingOffer?     // nil if not eligible or not configured
    @objc let promotionalOffers:  [BillingOffer]
    @objc let winBackOffers:      [BillingOffer]    // empty on macOS < 14

    @objc init(productId: String, title: String, productDescription: String,
               displayPrice: String, kind: BillingProductKind,
               subscriptionPeriod: BillingPeriod?, introductoryOffer: BillingOffer?,
               promotionalOffers: [BillingOffer], winBackOffers: [BillingOffer]) {
        self.productId = productId; self.title = title
        self.productDescription = productDescription; self.displayPrice = displayPrice
        self.kind = kind; self.subscriptionPeriod = subscriptionPeriod
        self.introductoryOffer = introductoryOffer; self.promotionalOffers = promotionalOffers
        self.winBackOffers = winBackOffers
    }
}

@objc(BillingTransaction)
class BillingTransaction: NSObject {
    @objc let transactionId: String
    @objc let originalId:    String
    @objc let productId:     String
    @objc let kind:          BillingProductKind
    @objc let purchasedAt:   Date
    @objc let expiresAt:     Date?
    @objc let revokedAt:     Date?
    @objc let redeemedOffer: BillingOffer?
    @objc let familyShared:  Bool
    @objc let upgraded:      Bool

    @objc init(transactionId: String, originalId: String, productId: String,
               kind: BillingProductKind, purchasedAt: Date, expiresAt: Date?,
               revokedAt: Date?, redeemedOffer: BillingOffer?,
               familyShared: Bool, upgraded: Bool) {
        self.transactionId = transactionId; self.originalId = originalId
        self.productId = productId; self.kind = kind; self.purchasedAt = purchasedAt
        self.expiresAt = expiresAt; self.revokedAt = revokedAt
        self.redeemedOffer = redeemedOffer; self.familyShared = familyShared
        self.upgraded = upgraded
    }
}

@objc(BillingEntitlement)
class BillingEntitlement: NSObject {
    @objc let productId:            String
    @objc let state:                BillingSubscriptionState
    @objc let transaction:          BillingTransaction
    @objc let expiresAt:            Date?
    @objc let hasExpirationReason:  Bool
    @objc let expirationReason:     BillingExpirationReason  // valid only if hasExpirationReason
    @objc let willAutoRenew:        Bool
    @objc let renewalProductId:     String?                  // nil if no tier switch pending

    @objc init(productId: String, state: BillingSubscriptionState,
               transaction: BillingTransaction, expiresAt: Date?,
               hasExpirationReason: Bool, expirationReason: BillingExpirationReason,
               willAutoRenew: Bool, renewalProductId: String?) {
        self.productId = productId; self.state = state; self.transaction = transaction
        self.expiresAt = expiresAt; self.hasExpirationReason = hasExpirationReason
        self.expirationReason = expirationReason; self.willAutoRenew = willAutoRenew
        self.renewalProductId = renewalProductId
    }
}

@objc(BillingPurchaseResult)
class BillingPurchaseResult: NSObject {
    @objc let productId:    String
    @objc let transaction:  BillingTransaction?  // set on success
    @objc let succeeded:    Bool
    @objc let errorCode:    BillingPurchaseError // only meaningful when !succeeded
    @objc let errorMessage: String?

    @objc init(productId: String, transaction: BillingTransaction?,
               succeeded: Bool, errorCode: BillingPurchaseError, errorMessage: String?) {
        self.productId = productId; self.transaction = transaction
        self.succeeded = succeeded; self.errorCode = errorCode
        self.errorMessage = errorMessage
    }

    static func success(productId: String, transaction: BillingTransaction) -> BillingPurchaseResult {
        BillingPurchaseResult(productId: productId, transaction: transaction,
                              succeeded: true, errorCode: .cancelled, errorMessage: nil)
    }

    static func failure(productId: String, code: BillingPurchaseError,
                        message: String? = nil) -> BillingPurchaseResult {
        BillingPurchaseResult(productId: productId, transaction: nil,
                              succeeded: false, errorCode: code, errorMessage: message)
    }
}