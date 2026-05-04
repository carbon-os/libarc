# billing/CMakeLists.txt

cmake_minimum_required(VERSION 3.26)
project(billing LANGUAGES CXX)

# ── Platform dispatch ─────────────────────────────────────────────────────────

if(APPLE)

    # Apple build requires Objective-C++ and Swift in addition to C++.
    enable_language(OBJCXX Swift)

    # ── Locate tools ──────────────────────────────────────────────────────────
    execute_process(
        COMMAND xcrun --sdk macosx --show-sdk-path
        OUTPUT_VARIABLE APPLE_SDK_PATH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY
    )

    execute_process(
        COMMAND xcrun -f swiftc
        OUTPUT_VARIABLE SWIFTC
        OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY
    )

    # ── Generated ObjC compatibility header ───────────────────────────────────
    set(SWIFT_OBJC_HDR "${CMAKE_CURRENT_BINARY_DIR}/billing_apple-Swift.h")

    set(BILLING_SWIFT_SRCS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/apple-store/swift/Models.swift"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/apple-store/swift/Converters.swift"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/apple-store/swift/ProductFetcher.swift"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/apple-store/swift/PurchaseHandler.swift"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/apple-store/swift/EntitlementManager.swift"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/apple-store/swift/PurchaseIntentListener.swift"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/apple-store/swift/RefundHandler.swift"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/apple-store/swift/StoreBridge.swift"
    )

    execute_process(
        COMMAND "${SWIFTC}"
                -wmo
                -sdk             "${APPLE_SDK_PATH}"
                -module-name     billing_apple
                -parse-as-library
                -swift-version   5
                -emit-module
                -emit-module-path "${CMAKE_CURRENT_BINARY_DIR}/billing_apple_hdrgen.swiftmodule"
                -emit-objc-header
                -emit-objc-header-path "${SWIFT_OBJC_HDR}"
                ${BILLING_SWIFT_SRCS}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE  _swift_hdr_result
        ERROR_VARIABLE   _swift_hdr_stderr
        COMMAND_ERROR_IS_FATAL ANY
    )

    message(STATUS "Generated ${SWIFT_OBJC_HDR}")

    # ── Target 1: Swift object library ────────────────────────────────────────
    add_library(billing_swift STATIC ${BILLING_SWIFT_SRCS})

    set_target_properties(billing_swift PROPERTIES
        Swift_LANGUAGE_VERSION 5
        Swift_MODULE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    )

    target_include_directories(billing_swift PUBLIC include)

    find_library(FW_STOREKIT   StoreKit   REQUIRED)
    find_library(FW_FOUNDATION Foundation REQUIRED)
    find_library(FW_APPKIT     AppKit     REQUIRED)

    target_link_libraries(billing_swift PUBLIC
        ${FW_STOREKIT}
        ${FW_FOUNDATION}
        ${FW_APPKIT}
    )

    # ── Target 2: ObjC++ shim (the public C++ target) ─────────────────────────
    add_library(billing_apple STATIC
        src/apple-store/ShimStore.mm
    )

    add_dependencies(billing_apple billing_swift)

    target_compile_features(billing_apple PUBLIC cxx_std_17)

    target_include_directories(billing_apple PUBLIC
        include
        "${CMAKE_CURRENT_BINARY_DIR}"
    )

    target_link_libraries(billing_apple PUBLIC billing_swift)

elseif(WIN32)

    # ── Windows: pure C++/WinRT — no extra language modes needed ──────────────

    add_library(billing_windows STATIC
        src/microsoft-store/Store.cpp
    )

    target_compile_features(billing_windows PUBLIC cxx_std_17)

    # Require Windows 10 1607+ (Build 14393) — minimum for Windows.Services.Store.
    # Also needed for correct WinRT header selection.
    target_compile_definitions(billing_windows PUBLIC
        _WIN32_WINNT=0x0A00      # Windows 10+
        WINVER=0x0A00
        NTDDI_VERSION=0x0A000004 # NTDDI_WIN10_RS1 (Anniversary Update / 14393)
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )

    # cppwinrt from vcpkg — prefer this over the (potentially older) SDK headers.
    find_package(cppwinrt CONFIG REQUIRED)

    target_link_libraries(billing_windows PUBLIC
        Microsoft::CppWinRT   # header-only projection + cppwinrt.exe tooling
        windowsapp.lib        # Windows.Services.Store and other WinRT contracts
        ole32.lib             # CoCreateGuid, CLSIDFromString
        runtimeobject.lib     # RoInitialize / WinRT activation
    )

    target_include_directories(billing_windows PUBLIC include)

endif()