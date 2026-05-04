# billing/CMakeLists.txt

cmake_minimum_required(VERSION 3.26)
project(billing LANGUAGES CXX Swift)

# ── Apple Store target ────────────────────────────────────────────────────────
# Mixed Swift + ObjC++ in one static lib.
# CMake generates billing_apple-Swift.h automatically from the Swift sources;
# ShimStore.mm imports it by that name.
# NOTE: Xcode generator recommended. Ninja has limited mixed-language support.

add_library(billing_apple STATIC
    # ObjC++ shim — bridges Swift ↔ C++
    src/apple-store/ShimStore.mm

    # Swift — StoreKit 2 implementation, split by responsibility
    src/apple-store/swift/Models.swift
    src/apple-store/swift/Converters.swift
    src/apple-store/swift/ProductFetcher.swift
    src/apple-store/swift/PurchaseHandler.swift
    src/apple-store/swift/EntitlementManager.swift
    src/apple-store/swift/PurchaseIntentListener.swift
    src/apple-store/swift/RefundHandler.swift
    src/apple-store/swift/StoreBridge.swift
)

set_target_properties(billing_apple PROPERTIES
    Swift_LANGUAGE_VERSION 5
    # ObjC ARC on the .mm side
    XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
    # Name the generated Swift header so ShimStore.mm can import it reliably
    XCODE_ATTRIBUTE_SWIFT_OBJC_INTERFACE_HEADER_NAME "billing_apple-Swift.h"
    XCODE_ATTRIBUTE_SWIFT_INSTALL_OBJC_HEADER NO
)

target_include_directories(billing_apple PUBLIC include)

find_library(FW_STOREKIT   StoreKit   REQUIRED)
find_library(FW_FOUNDATION Foundation REQUIRED)
find_library(FW_APPKIT     AppKit     REQUIRED)

target_link_libraries(billing_apple PUBLIC
    ${FW_STOREKIT}
    ${FW_FOUNDATION}
    ${FW_APPKIT}
)