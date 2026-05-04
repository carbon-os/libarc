# host/CMakeLists.txt

add_library(host STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/host.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dispatcher.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main_thread.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/registry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/billing_manager.cpp
)

target_compile_features(host PUBLIC cxx_std_20)

target_include_directories(host PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(host PUBLIC
    ipc
    ui
    webview
    nlohmann_json::nlohmann_json
)

# ── Billing integration ───────────────────────────────────────────────────────
if(APPLE)
    target_link_libraries(host PRIVATE billing_apple)
elseif(WIN32)
    target_link_libraries(host PRIVATE billing_windows)
endif()

# ── macOS: compile main_thread.cpp as Objective-C++ ──────────────────────────
if(APPLE)
    set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/src/main_thread.cpp
        PROPERTIES COMPILE_FLAGS "-x objective-c++ -fobjc-arc"
    )
    find_library(FW_FOUNDATION Foundation REQUIRED)
    find_library(FW_APPKIT     AppKit     REQUIRED)
    target_link_libraries(host PRIVATE ${FW_FOUNDATION} ${FW_APPKIT})
endif()

# ── Linux: GTK3 for the event loop ───────────────────────────────────────────
if(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
    target_include_directories(host PRIVATE ${GTK3_INCLUDE_DIRS})
    target_link_libraries(host PRIVATE ${GTK3_LIBRARIES})
endif()