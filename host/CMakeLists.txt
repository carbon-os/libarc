cmake_minimum_required(VERSION 3.21)

add_library(host STATIC
    src/host.cpp
    src/registry.cpp
    src/dispatcher.cpp
    src/main_thread.cpp
)

target_include_directories(host
    PUBLIC  include
    PRIVATE src
)

target_compile_features(host PUBLIC cxx_std_20)

# nlohmann-json comes from vcpkg — still needs find_package
find_package(nlohmann_json CONFIG REQUIRED)

# libui, libwebview, libipc are sibling add_subdirectory targets — no find_package
target_link_libraries(host
    PUBLIC
        ui
        webview
        ipc
        nlohmann_json::nlohmann_json
)

if(APPLE)
    set_source_files_properties(src/main_thread.cpp PROPERTIES
        COMPILE_FLAGS "-x objective-c++")
    target_link_libraries(host PUBLIC
        "-framework Foundation"
        "-framework AppKit")
    target_compile_options(host PRIVATE -fobjc-arc)
elseif(UNIX)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
    target_include_directories(host PRIVATE ${GTK3_INCLUDE_DIRS})
    target_link_libraries(host PRIVATE ${GTK3_LINK_LIBRARIES})
endif()