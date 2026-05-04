cmake_minimum_required(VERSION 3.21)
project(libarc VERSION 2.0.0 LANGUAGES CXX Swift)

# ── macOS deployment target ───────────────────────────────────────────────────
# Set before any add_subdirectory so all targets inherit it.
if(APPLE)
    if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS version")
    endif()
    # Suppress the per-target MACOSX_RPATH warning
    set(CMAKE_MACOSX_RPATH ON)
endif()

find_package(nlohmann_json CONFIG REQUIRED)

add_subdirectory(ipc)
add_subdirectory(ui)
add_subdirectory(webview)
add_subdirectory(host)
add_subdirectory(arc-host)

# ── Integrations ──────────────────────────────────────────────────────────────
if(APPLE)
    add_subdirectory(integrations/billing)
endif()