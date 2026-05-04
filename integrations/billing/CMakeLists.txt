# billing/CMakeLists.txt

cmake_minimum_required(VERSION 3.26)
project(billing LANGUAGES CXX OBJCXX Swift)

# ── Locate tools ──────────────────────────────────────────────────────────────
execute_process(
    COMMAND xcrun --sdk macosx --show-sdk-path
    OUTPUT_VARIABLE APPLE_SDK_PATH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
)

# Use xcrun to resolve swiftc so we get the toolchain-correct binary regardless
# of PATH.  CMAKE_Swift_COMPILER is not guaranteed to be set yet at the point
# execute_process runs during this subdirectory's configure pass.
execute_process(
    COMMAND xcrun -f swiftc
    OUTPUT_VARIABLE SWIFTC
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
)

# ── Where the generated ObjC compatibility header will land ───────────────────
set(SWIFT_OBJC_HDR "${CMAKE_CURRENT_BINARY_DIR}/billing_apple-Swift.h")

# ── Swift source list ─────────────────────────────────────────────────────────
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

# ── Generate the ObjC compatibility header at configure time ──────────────────
#
# All previous attempts ran swiftc inside the Ninja build graph.  The problem
# is that CMake's Ninja backend also compiles billing_swift per-file, and those
# per-file swiftc invocations write (or truncate) billing_apple-Swift.h as a
# side-effect, racing or overwriting the header our custom command produced.
#
# Running it here, during cmake configure, sidesteps the build graph entirely.
# The header is written to disk before Ninja starts, so nothing can race it.
# It re-runs on every `cmake` reconfigure (i.e. when you change a .swift file
# and re-run cmake), which is the correct invalidation granularity for a
# configure-time step.
#
# -emit-module (not -typecheck) is required: -typecheck exits before output
# emission and leaves the header file empty.

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

# ── Target 1: Swift library (object code) ─────────────────────────────────────
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

# ── Target 2: ObjC++ shim ─────────────────────────────────────────────────────
add_library(billing_apple STATIC
    src/apple-store/ShimStore.mm
)

# billing_swift must be fully built before billing_apple links, since
# billing_apple links against it.  The header is already on disk from the
# configure step above, so no custom target is needed for ordering.
add_dependencies(billing_apple billing_swift)

# Enforce C++17 on this target explicitly. ShimStore.mm uses std::optional,
# std::nullopt, and nested namespace syntax (billing::apple), all of which
# require C++17. The root CMakeLists sets CMAKE_CXX_STANDARD globally, but
# OBJCXX is a separate language slot in CMake and doesn't always inherit it
# without an explicit requirement here.
target_compile_features(billing_apple PUBLIC cxx_std_17)

target_include_directories(billing_apple PUBLIC
    include
    "${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(billing_apple PUBLIC billing_swift)