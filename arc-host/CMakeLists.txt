cmake_minimum_required(VERSION 3.21)

add_executable(arc-host main.cpp)
target_compile_features(arc-host PRIVATE cxx_std_20)
target_link_libraries(arc-host PRIVATE host)

if(UNIX)
    target_link_libraries(arc-host PRIVATE ${CMAKE_DL_LIBS})
endif()

# ── macOS code signing + entitlements ────────────────────────────────────────
if(APPLE)
    set(ENTITLEMENTS_FILE "${CMAKE_CURRENT_SOURCE_DIR}/app.entitlements")

    # Sign identity: "-" = ad-hoc (works for local dev + devtools).
    # Override at configure time:
    #   cmake -DARC_SIGN_IDENTITY="Apple Development: ..." ...
    if(NOT DEFINED ARC_SIGN_IDENTITY)
        set(ARC_SIGN_IDENTITY "-")
    endif()

    # Embed entitlements into the binary after it is linked.
    add_custom_command(
        TARGET arc-host POST_BUILD
        COMMAND codesign
            --force
            --sign "${ARC_SIGN_IDENTITY}"
            --entitlements "${ENTITLEMENTS_FILE}"
            --options runtime          # hardened runtime
            $<TARGET_FILE:arc-host>
        COMMENT "Signing arc-host with entitlements (identity: ${ARC_SIGN_IDENTITY})"
        VERBATIM
    )
endif()