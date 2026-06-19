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
    message(STATUS "REDesk third_party: real backends ON — add vendored deps here")
    # Example (uncomment + vendor as implemented):
    #   add_subdirectory(libsodium)   # ISC — crypto core
    #   add_subdirectory(libjuice)    # MPL-2.0 — ICE/NAT (do NOT edit sources)
    #   add_subdirectory(noise-c)     # MIT — Noise protocol
    #   add_subdirectory(cm256)       # BSD — FEC
endif()
