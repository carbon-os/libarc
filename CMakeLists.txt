cmake_minimum_required(VERSION 3.21)
project(libarc VERSION 2.0.0 LANGUAGES CXX)

# ── C++ standard ──────────────────────────────────────────────────────────────
# C++17 required for: std::optional, std::nullopt, nested namespace syntax,
# structured bindings, and if-constexpr used throughout libarc and the billing
# shim. Set it here so every subdirectory inherits it without repetition.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── macOS deployment target ───────────────────────────────────────────────────
if(APPLE)
    if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS version")
    endif()
    set(CMAKE_MACOSX_RPATH ON)
endif()

find_package(nlohmann_json CONFIG REQUIRED)

add_subdirectory(ipc)
add_subdirectory(ui)
add_subdirectory(webview)
add_subdirectory(host)
add_subdirectory(arc-host)

# ── Integrations ──────────────────────────────────────────────────────────────
if(APPLE OR WIN32)
    add_subdirectory(integrations/billing)
endif()