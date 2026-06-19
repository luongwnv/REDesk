# platform/ — per-OS native backends (ADR-001 §3.1 capture, §3.2 encode,
# §3.4 input injection, §3.6 keystore).
#
# Exactly ONE backend subdirectory is compiled, chosen by REDESK_PLATFORM
# (set in the top-level CMakeLists from WIN32/APPLE/UNIX). The selected dir
# produces an OBJECT/STATIC contribution that is folded into the single
# target `redesk_platform` (ALIAS REDesk::platform), which links REDesk::core.
#
# Build contract (HARD RULE 2): with REDESK_USE_REAL_BACKENDS=OFF this target
# MUST compile on any host with no external dependencies. Every real backend
# body is guarded by `#if REDESK_USE_REAL_BACKENDS`, so the default build is a
# set of thin stub-registration shims / empty translation units. The macOS
# ObjC++ (.mm) sources are additionally only *added to the build* when
# (APPLE AND REDESK_USE_REAL_BACKENDS) so a default macOS build never invokes
# the ObjC toolchain or links Apple frameworks.

# ---------------------------------------------------------------------------
# Aggregate target. We collect the chosen backend's sources here and build a
# single library so downstream (service/, ui/) links one well-named target.
# ---------------------------------------------------------------------------
add_library(redesk_platform STATIC)
add_library(REDesk::platform ALIAS redesk_platform)

target_link_libraries(redesk_platform PUBLIC REDesk::core)

# Repo-root include style ("core/common/types.h", "platform/windows/...").
target_include_directories(redesk_platform
    PUBLIC
        "${CMAKE_SOURCE_DIR}"
        "${CMAKE_BINARY_DIR}/generated"   # redesk_version.h
)

# Surface the master toggle into the preprocessor as a 0/1 value so backend
# sources can use `#if REDESK_USE_REAL_BACKENDS`. (core/ is expected to define
# the same macro for its stub factories; if it does not, defining it PUBLIC
# here keeps platform-facing consumers consistent.)
if(REDESK_USE_REAL_BACKENDS)
    target_compile_definitions(redesk_platform PUBLIC REDESK_USE_REAL_BACKENDS=1)
else()
    target_compile_definitions(redesk_platform PUBLIC REDESK_USE_REAL_BACKENDS=0)
endif()

target_compile_features(redesk_platform PUBLIC cxx_std_20)

# ---------------------------------------------------------------------------
# Dispatch to exactly one backend dir. Each subdir appends its sources to
# `redesk_platform` via target_sources() and adds its own link deps (guarded
# on REDESK_USE_REAL_BACKENDS inside the subdir).
# ---------------------------------------------------------------------------
if(REDESK_PLATFORM STREQUAL "windows")
    add_subdirectory(windows)
elseif(REDESK_PLATFORM STREQUAL "macos")
    add_subdirectory(macos)
elseif(REDESK_PLATFORM STREQUAL "linux")
    add_subdirectory(linux)
else()
    message(FATAL_ERROR "platform/: unknown REDESK_PLATFORM='${REDESK_PLATFORM}'")
endif()

message(STATUS "platform/: building '${REDESK_PLATFORM}' backend "
               "(real=${REDESK_USE_REAL_BACKENDS}) into REDesk::platform")
