# redesk-rendezvous — signaling / zero-knowledge server (ADR-001 §3.3, §3.6).
#
# ID->endpoint registry + ICE/SDP broker that Ed25519-signs each peer's static
# pubkey and has zero knowledge of media keys. (== RustDesk hbbs.)
#
# Stub build (default): InsecureTestSigner + in-memory registry, std + the
# portable socket wrapper only — no external deps. The real Ed25519 path
# (libsodium) compiles in only when REDESK_USE_REAL_BACKENDS=ON.

add_executable(redesk-rendezvous
    main.cpp
    protocol.cpp
    protocol.h
    registry_store.cpp
    registry_store.h
    signer.cpp
    signer.h
)

target_link_libraries(redesk-rendezvous PRIVATE
    REDesk::core
    REDesk::server_net
)

# Repo-root-relative includes ("server/...", "core/...") + generated version.h.
target_include_directories(redesk-rendezvous PRIVATE
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_BINARY_DIR}/generated"
)

if(REDESK_USE_REAL_BACKENDS)
    # Real Ed25519 signer. libsodium (ISC) is the single crypto core (ADR §4),
    # located/aliased in third_party/ as REDesk::sodium.
    target_compile_definitions(redesk-rendezvous PRIVATE REDESK_USE_REAL_BACKENDS=1)
    if(TARGET REDesk::sodium)
        target_link_libraries(redesk-rendezvous PRIVATE REDesk::sodium)
    else()
        message(WARNING "redesk-rendezvous: REDesk::sodium missing; real signer won't link.")
    endif()
endif()
