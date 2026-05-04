cmake_minimum_required(VERSION 3.22)

# Only require CXX by default
project(sample_ipc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable Objective-C++ and set its standard conditionally for macOS
if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 20)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
endif()

# ── Locate libui and libwebview ───────────────────────────────────────────────
# When built as part of the libarc workspace the ui and webview targets are
# already defined by the time CMake processes this directory.  If this sample
# is ever built standalone, point CMAKE_PREFIX_PATH at the install tree and
# swap the two lines below for find_package calls instead.
if(NOT TARGET ui)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../ui
                     ${CMAKE_BINARY_DIR}/ui)
endif()

if(NOT TARGET webview)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../webview
                     ${CMAKE_BINARY_DIR}/webview)
endif()

# ── Sample executable ─────────────────────────────────────────────────────────
add_executable(sample_ipc main_ipc.cpp)

target_link_libraries(sample_ipc PRIVATE ui webview)

# macOS: make it a proper app bundle so the run loop and window server work
if(APPLE)
    set_target_properties(sample_ipc PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME    "IPC Sample"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.libarc.sample.ipc"
        MACOSX_BUNDLE_BUNDLE_VERSION "1.0"
    )
endif()