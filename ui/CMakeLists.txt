cmake_minimum_required(VERSION 3.21)

# Only require CXX by default
project(libui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Sources ───────────────────────────────────────────────────────────────────
set(UI_SOURCES "")

if(APPLE)
    # Enable Objective-C++ and set its standard conditionally
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 17)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)

    list(APPEND UI_SOURCES
        src/macos/Window.mm
        src/macos/View.mm
        src/macos/PixelView.mm
    )
elseif(WIN32)
    list(APPEND UI_SOURCES
        src/windows/Window.cpp
        src/windows/View.cpp
        src/windows/PixelView.cpp
    )
elseif(UNIX AND NOT APPLE)
    list(APPEND UI_SOURCES
        src/linux/Window.cpp
        src/linux/View.cpp
        src/linux/PixelView.cpp
    )
endif()

# ── Library target ────────────────────────────────────────────────────────────
add_library(ui STATIC ${UI_SOURCES})

target_include_directories(ui
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(APPLE)
    find_library(COCOA_FW  Cocoa      REQUIRED)
    find_library(QUARTZ_FW QuartzCore REQUIRED)

    target_link_libraries(ui PUBLIC ${COCOA_FW} ${QUARTZ_FW})

    target_compile_options(ui PRIVATE -fobjc-arc)

    target_compile_definitions(ui PUBLIC
        UI_PLATFORM="macos"
        UI_FEATURE_BACKDROP_EFFECT
    )
endif()

if(WIN32)
    target_link_libraries(ui PUBLIC dwmapi user32 gdi32 shell32)

    target_compile_definitions(ui PUBLIC
        UI_PLATFORM="windows"
        UI_FEATURE_BACKDROP_EFFECT
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()

if(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED gtk+-3.0)

    target_link_libraries(ui PUBLIC ${GTK3_LIBRARIES})
    target_include_directories(ui PUBLIC ${GTK3_INCLUDE_DIRS})
    target_compile_options(ui PUBLIC ${GTK3_CFLAGS_OTHER})

    target_compile_definitions(ui PUBLIC
        UI_PLATFORM="linux"
    )
endif()

# ── Install ───────────────────────────────────────────────────────────────────
include(GNUInstallDirs)
install(TARGETS ui ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ui DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})