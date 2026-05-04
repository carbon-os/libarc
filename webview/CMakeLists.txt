cmake_minimum_required(VERSION 3.21)
project(libwebview LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Dependencies ──────────────────────────────────────────────────────────────
find_package(nlohmann_json CONFIG REQUIRED)

# ── Sources ───────────────────────────────────────────────────────────────────
set(WEBVIEW_SOURCES "")

if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 20)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
    list(APPEND WEBVIEW_SOURCES
        src/wkwebview/WebViewImpl.mm
        src/wkwebview/navigation.mm
        src/wkwebview/events.mm
        src/wkwebview/script.mm
        src/wkwebview/ipc.mm
        src/wkwebview/cookies.mm
        src/wkwebview/media.mm
        src/wkwebview/ui.mm
        src/wkwebview/request.mm
        src/wkwebview/dialog.mm
    )
elseif(WIN32)
    list(APPEND WEBVIEW_SOURCES
        src/webview2/WebViewImpl.cpp
        src/webview2/navigation.cpp
        src/webview2/events.cpp
        src/webview2/script.cpp
        src/webview2/ui.cpp
        src/webview2/cookies.cpp
        src/webview2/dialog.cpp
        src/webview2/ipc.cpp
        src/webview2/media.cpp
        src/webview2/request.cpp
    )
elseif(UNIX AND NOT APPLE)
    list(APPEND WEBVIEW_SOURCES
        src/webkit/WebViewImpl.cpp
        src/webkit/navigation.cpp
        src/webkit/events.cpp
        src/webkit/script.cpp
        src/webkit/ui.cpp
        src/webkit/cookies.cpp
        src/webkit/dialog.cpp
        src/webkit/ipc.cpp
        src/webkit/media.cpp
        src/webkit/request.cpp
    )
endif()

# ── Library target ────────────────────────────────────────────────────────────
add_library(webview STATIC ${WEBVIEW_SOURCES})

target_include_directories(webview
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(webview PUBLIC nlohmann_json::nlohmann_json)

if(APPLE)
    find_library(WEBKIT_FW WebKit REQUIRED)
    find_library(COCOA_FW  Cocoa  REQUIRED)
    find_library(UNIFORMTYPEIDENTIFIERS_FW UniformTypeIdentifiers)

    target_link_libraries(webview PUBLIC ${WEBKIT_FW} ${COCOA_FW})
    if(UNIFORMTYPEIDENTIFIERS_FW)
        target_link_libraries(webview PUBLIC ${UNIFORMTYPEIDENTIFIERS_FW})
    endif()

    target_compile_options(webview PRIVATE -fobjc-arc)

    target_compile_definitions(webview PUBLIC
        WEBVIEW_PLATFORM="macos"
        WEBVIEW_FEATURE_REQUEST_INTERCEPT
    )
endif()

if(WIN32)
    find_package(unofficial-webview2 CONFIG REQUIRED)

    target_link_libraries(webview PUBLIC
        unofficial::webview2::webview2
        shlwapi
        comctl32
        ole32
        shell32
        uuid
    )

    target_compile_definitions(webview PUBLIC
        WEBVIEW_PLATFORM="windows"
        WEBVIEW_FEATURE_REQUEST_INTERCEPT
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        UNICODE
        _UNICODE
    )
endif()

if(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    
    # We target GTK3 and WebKit2GTK 4.1 (which supports the POST body API used in ipc.cpp)
    pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
    pkg_check_modules(WEBKIT2GTK REQUIRED IMPORTED_TARGET webkit2gtk-4.1)

    target_link_libraries(webview PUBLIC 
        PkgConfig::GTK3 
        PkgConfig::WEBKIT2GTK
    )

    target_compile_definitions(webview PUBLIC
        WEBVIEW_PLATFORM="linux"
        # WEBVIEW_FEATURE_REQUEST_INTERCEPT is omitted as synchronous full blocking 
        # isn't natively supported on WebKitGTK without a custom WebProcess extension.
    )
endif()

# ── Install ───────────────────────────────────────────────────────────────────
include(GNUInstallDirs)
install(TARGETS webview ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/webview DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})