# REDesk — headless privileged service / daemon (ADR-001 §2, §3.5).
#
# Builds the 'redesk-service' executable. It links REDesk::core (engine
# interfaces + session manager) and REDesk::platform (OS capture/encode/input/
# keystore backends). In the default stub build (REDESK_USE_REAL_BACKENDS=OFF)
# it runs an end-to-end smoke pipeline with no external deps.

add_executable(redesk-service
    src/main.cpp
    src/ipc_server.cpp
    src/service_lifecycle.cpp
)

# ---------------------------------------------------------------------------
# Engine contract seam.
#
# The service drives core/ + platform/ engine objects via their factories. Once
# those slices export their interface headers + factories, set
# REDESK_HAVE_ENGINE_HEADERS so the service includes the canonical headers and
# links the real symbols from REDesk::core / REDesk::platform.
#
# Until then we compile the fallback mirror+stubs in engine_stubs.cpp so
# `redesk-service --foreground` runs a real smoke pipeline today (HARD RULE 2:
# the stub build must compile with no missing deps).
#
# INTEGRATOR: flip this ON (and reconcile include paths/signatures in
# service/src/engine_contracts.h) when core/platform land.
# ---------------------------------------------------------------------------
set(REDESK_SERVICE_HAVE_ENGINE_HEADERS OFF CACHE BOOL
    "Service uses the real core/platform engine headers instead of its fallback mirror")

if(REDESK_SERVICE_HAVE_ENGINE_HEADERS)
    target_compile_definitions(redesk-service PRIVATE REDESK_HAVE_ENGINE_HEADERS)
else()
    target_sources(redesk-service PRIVATE src/engine_stubs.cpp)
endif()

target_compile_features(redesk-service PRIVATE cxx_std_20)

# Repo-root-relative includes ("core/common/types.h", "service/src/...") plus the
# generated version header from the build dir.
target_include_directories(redesk-service PRIVATE
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_BINARY_DIR}/generated"
)

# ---------------------------------------------------------------------------
# Engine libraries. Always link core; link platform when available. These ALIAS
# targets are provided by core/ and platform/ CMakeLists. If a slice is absent
# during early scaffolding, guard so the service still configures.
# ---------------------------------------------------------------------------
if(TARGET REDesk::core)
    target_link_libraries(redesk-service PRIVATE REDesk::core)
endif()
if(TARGET REDesk::platform)
    target_link_libraries(redesk-service PRIVATE REDesk::platform)
endif()

# Threads for the IPC accept loop + run loop.
find_package(Threads REQUIRED)
target_link_libraries(redesk-service PRIVATE Threads::Threads)

# ---------------------------------------------------------------------------
# Real OS service lifecycle backends (sketched in service_lifecycle.cpp). Enable
# when implementing SCM/launchd/systemd integration; off in the stub build.
# ---------------------------------------------------------------------------
option(REDESK_SERVICE_REAL_LIFECYCLE
    "Compile the real OS service lifecycle backend (SCM/launchd/systemd)" OFF)
if(REDESK_SERVICE_REAL_LIFECYCLE)
    target_compile_definitions(redesk-service PRIVATE REDESK_REAL_LIFECYCLE)
    if(UNIX AND NOT APPLE)
        # systemd sd_notify / watchdog integration (Type=notify).
        # find_package(PkgConfig); pkg_check_modules(SYSTEMD libsystemd) ...
        # target_link_libraries(redesk-service PRIVATE ${SYSTEMD_LIBRARIES})
    elseif(WIN32)
        target_link_libraries(redesk-service PRIVATE advapi32)  # SCM APIs
    endif()
endif()

# Install the binary + the OS service templates from dist/.
install(TARGETS redesk-service RUNTIME DESTINATION bin)
install(DIRECTORY dist/ DESTINATION share/redesk/service-dist)
