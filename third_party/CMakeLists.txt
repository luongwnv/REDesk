# Third-party dependencies.
#
# Real deps (libsodium, libjuice, noise-c, FFmpeg, cm256, KCP, ...) are vendored
# as submodules / FetchContent under this dir and added here, gated on
# REDESK_USE_REAL_BACKENDS. See docs/adr/ADR-001 §4 for the full list + license
# flags. Each vendored dir must carry its upstream LICENSE and is audited per
# the AGPL/GPL/MPL notes in the ADR.
#
# In the default stub build there are no external dependencies, so this file is
# intentionally near-empty.

if(REDESK_USE_REAL_BACKENDS)
    message(STATUS "REDesk third_party: real backends ON — locating system deps")

    # libsodium (ISC) — the single crypto core (ADR §4). Prefer a vendored copy
    # under third_party/libsodium; otherwise fall back to a system install
    # (pkg-config / common prefixes incl. Homebrew). Exposed as REDesk::sodium.
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(SODIUM IMPORTED_TARGET libsodium)
    endif()

    if(TARGET PkgConfig::SODIUM)
        add_library(REDesk::sodium ALIAS PkgConfig::SODIUM)
        message(STATUS "  libsodium: found via pkg-config")
    else()
        find_path(SODIUM_INCLUDE_DIR sodium.h
            HINTS /opt/homebrew/opt/libsodium/include /usr/local/include /usr/include)
        find_library(SODIUM_LIBRARY NAMES sodium libsodium
            HINTS /opt/homebrew/opt/libsodium/lib /usr/local/lib /usr/lib)
        if(SODIUM_INCLUDE_DIR AND SODIUM_LIBRARY)
            add_library(redesk_sodium INTERFACE)
            target_include_directories(redesk_sodium INTERFACE "${SODIUM_INCLUDE_DIR}")
            target_link_libraries(redesk_sodium INTERFACE "${SODIUM_LIBRARY}")
            add_library(REDesk::sodium ALIAS redesk_sodium)
            message(STATUS "  libsodium: found at ${SODIUM_LIBRARY}")
        else()
            message(WARNING "  libsodium NOT found — real crypto targets will not link. "
                            "Install libsodium or vendor it under third_party/libsodium.")
        endif()
    endif()

    # Example (vendor as implemented):
    #   add_subdirectory(libjuice)    # MPL-2.0 — ICE/NAT (do NOT edit sources)
    #   add_subdirectory(noise-c)     # MIT — Noise protocol
    #   add_subdirectory(cm256)       # BSD — FEC
endif()
