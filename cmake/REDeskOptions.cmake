# REDesk build options & feature toggles.
#
# The scaffold builds out-of-the-box with REDESK_USE_REAL_BACKENDS=OFF: every
# platform/codec/transport seam compiles to a stub so the architecture and IPC
# wiring can be exercised without Qt, FFmpeg, or vendor SDKs installed. Flip the
# toggles on as each real backend is implemented (see docs/adr/ADR-001).

include_guard(GLOBAL)

# Master switch. OFF -> portable stub build (no external deps, useful for CI of
# the core engine + protocol). ON -> link real Qt/FFmpeg/SDK backends.
option(REDESK_USE_REAL_BACKENDS "Link real capture/codec/transport backends instead of stubs" OFF)

# Component toggles.
option(REDESK_BUILD_UI       "Build the Qt Quick UI client" ON)
option(REDESK_BUILD_SERVICE  "Build the headless service/daemon" ON)
option(REDESK_BUILD_SERVERS  "Build rendezvous + relay servers" ON)
option(REDESK_BUILD_TESTS    "Build the test suite" ON)

# Feature toggles (only meaningful when REDESK_USE_REAL_BACKENDS=ON).
option(REDESK_ENABLE_HEVC    "Enable HEVC codec tier" ON)
option(REDESK_ENABLE_AV1     "Enable opportunistic AV1 codec tier" ON)
option(REDESK_ENABLE_PQ      "Enable hybrid post-quantum key exchange (X25519+ML-KEM-768)" OFF)

# Sanitizers (dev builds).
option(REDESK_ENABLE_ASAN    "Enable AddressSanitizer" OFF)
option(REDESK_ENABLE_UBSAN   "Enable UndefinedBehaviorSanitizer" OFF)

if(REDESK_ENABLE_ASAN OR REDESK_ENABLE_UBSAN)
    include(Sanitizers)
endif()

# The UI requires Qt. If the toolchain can't find Qt6 and real backends are off,
# we still want a configurable tree, so UI gracefully self-disables (see ui/).
