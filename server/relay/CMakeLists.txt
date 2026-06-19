# redesk-relay — thin blind-forward relay (ADR-001 §3.3, §3.6.2).
#
# Forwards opaque Noise ciphertext between two endpoints by session token and
# never decrypts. The default punch-fail fallback (== RustDesk hbbr); coturn/
# eturnal are reserved for the standards-TURN web-client path.
#
# Stub build (default): in-memory allocation table, std + the portable socket
# wrapper only — no external deps. The relay holds no key material, so there is
# no REDESK_USE_REAL_BACKENDS crypto gate here; "real backend" for the relay is
# the distributed/anycast allocation table + event loop (see TODOs in source).

add_executable(redesk-relay
    main.cpp
    allocation_table.cpp
    allocation_table.h
)

target_link_libraries(redesk-relay PRIVATE
    REDesk::core
    REDesk::server_net
)

target_include_directories(redesk-relay PRIVATE
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_BINARY_DIR}/generated"
)
