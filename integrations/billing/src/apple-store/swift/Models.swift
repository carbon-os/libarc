// src/apple-store/swift/Models.swift
// @objc bridge types — the vocabulary the shim speaks.
// Only ObjC-compatible types at the boundary: NSObject subclasses,
// NSString, NSArray, NSDate, BOOL, Int, and @objc enums.

import Foundation
import StoreKit

// MARK: - Enums

@objc public enum BillingProductKind: Int {
    case consumable = 0
    case nonConsumable = 1
    case autoRenewableSubscription = 2
    case nonRenewingSubscription = 3
}

@objc public enum BillingOfferType: Int {
    case introductory = 0
    case promotional  = 1
    case winBack      = 2
    case offerCode    = 3
}

@objc public enum BillingPaymentMode: Int {
    case freeTrial  = 0
    case payAsYouGo = 1
    case payUpFront = 2
}

@objc public enum BillingPeriodUnit: Int {
    case day   = 0
    case week  = 1
    case month = 2
    case year  = 3
}

@objc public enum BillingSubscriptionState: Int {
    case active              = 0
    case expired             = 1
    case inBillingRetry      = 2
    case inBillingGracePeriod = 3
    case revoked             = 4
}

@objc public enum BillingExpirationReason: Int {
    case cancelled          = 0
    case billingError       = 1
    case priceIncrease      = 2
    case productUnavailable = 3
    case unknown            = 4
}

@objc public enum BillingPurchaseError: Int {
    case cancelled            = 0
    case paymentFailed        = 1
    case productNotFound      = 2
    case notEntitled          = 3
    case pendingAuthorization = 4
    case networkError         = 5
    case unknown              = 6
}

@objc public enum BillingRefundStatus: Int {
    case success       = 0
    case userCancelled = 1
    case error         = 2
}

// MARK: - Value objects

@objc(BillingPeriod)
public class BillingPeriod: NSObject {
    @objc public let value: Int
    @objc public let unit:  BillingPeriodUnit
    
    @objc public init(value: Int, unit: BillingPeriodUnit) {
        self.value = value; self.unit = unit
    }
}

@objc(BillingOffer)
public class BillingOffer: NSObject {
    @objc public let offerId:      String
    @objc public let type:         BillingOfferType
    @objc public let paymentMode:  BillingPaymentMode
    @objc public let displayPrice: String
    @objc public let period:       BillingPeriod
    
    @objc public init(offerId: String, type: BillingOfferType, paymentMode: BillingPaymentMode,
               displayPrice: String, period: BillingPeriod) {
        self.offerId = offerId; self.type = type; self.paymentMode = paymentMode
        self.displayPrice = displayPrice; self.period = period
    }
}

@objc(BillingProduct)
public class BillingProduct: NSObject {
    @objc public let productId:          String
    @objc public let title:              String
    @objc public let productDescription: String
    @objc public let displayPrice:       String
    @objc public let kind:               BillingProductKind
    @objc public let subscriptionPeriod: BillingPeriod?    // nil for non-subscriptions
    @objc public let introductoryOffer:  BillingOffer?     // nil if not eligible or not configured
    @objc public let promotionalOffers:  [BillingOffer]
    @objc public let winBackOffers:      [BillingOffer]    // empty on macOS < 14

    @objc public init(productId: String, title: String, productDescription: String,
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
public class BillingTransaction: NSObject {
    @objc public let transactionId: String
    @objc public let originalId:    String
    @objc public let productId:     String
    @objc public let kind:          BillingProductKind
    @objc public let purchasedAt:   Date
    @objc public let expiresAt:     Date?
    @objc public let revokedAt:     Date?
    @objc public let redeemedOffer: BillingOffer?
    @objc public let familyShared:  Bool
    @objc public let upgraded:      Bool

    @objc public init(transactionId: String, originalId: String, productId: String,
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
public class BillingEntitlement: NSObject {
    @objc public let productId:            String
    @objc public let state:                BillingSubscriptionState
    @objc public let transaction:          BillingTransaction
    @objc public let expiresAt:            Date?
    @objc public let hasExpirationReason:  Bool
    @objc public let expirationReason:     BillingExpirationReason  // valid only if hasExpirationReason
    @objc public let willAutoRenew:        Bool
    @objc public let renewalProductId:     String?                  // nil if no tier switch pending

    @objc public init(productId: String, state: BillingSubscriptionState,
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
public class BillingPurchaseResult: NSObject {
    @objc public let productId:    String
    @objc public let transaction:  BillingTransaction?  // set on success
    @objc public let succeeded:    Bool
    @objc public let errorCode:    BillingPurchaseError // only meaningful when !succeeded
    @objc public let errorMessage: String?

    @objc public init(productId: String, transaction: BillingTransaction?,
               succeeded: Bool, errorCode: BillingPurchaseError, errorMessage: String?) {
        self.productId = productId; self.transaction = transaction
        self.succeeded = succeeded; self.errorCode = errorCode
        self.errorMessage = errorMessage
    }

    public static func success(productId: String, transaction: BillingTransaction) -> BillingPurchaseResult {
        BillingPurchaseResult(productId: productId, transaction: transaction,
                              succeeded: true, errorCode: .cancelled, errorMessage: nil)
    }

    public static func failure(productId: String, code: BillingPurchaseError,
                        message: String? = nil) -> BillingPurchaseResult {
        BillingPurchaseResult(productId: productId, transaction: nil,
                              succeeded: false, errorCode: code, errorMessage: message)
    }
}