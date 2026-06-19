# ---------------------------------------------------------------------------
# core/ — the platform-abstracted REDesk engine (ADR-001 §3).
#
# Builds two targets:
#   * redesk_common (core/common): shared value types + leveled logging.
#   * redesk_core   (this file)  : capture/codec/transport/crypto/input/session,
#                                  each a pure-virtual interface + a default stub
#                                  compiled when REDESK_USE_REAL_BACKENDS=OFF.
#
# core contains NO OS-specific code (that lives under platform/). The stub build
# has zero external dependencies, so it configures and compiles on any host with
# a C++20 compiler — no Qt, FFmpeg, or vendor SDKs required.
#
# redesk_core links ONLY redesk_common + the standard library.
# ---------------------------------------------------------------------------

# Repo-root + generated-dir paths, shared with subdir CMakeLists via cache-less
# normal variables (set in this scope, visible to add_subdirectory children).
set(REDESK_REPO_ROOT "${CMAKE_SOURCE_DIR}")
set(REDESK_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")

# Foundation first (defines redesk_common / REDesk::common).
add_subdirectory(common)

# ---------------------------------------------------------------------------
# redesk_core static library. Sources are attached by each sub-CMakeLists via
# target_sources(redesk_core PRIVATE ...). We declare the target empty here and
# let the subdirectories populate it, keeping each layer's file list local.
# ---------------------------------------------------------------------------
add_library(redesk_core STATIC)
add_library(REDesk::core ALIAS redesk_core)

target_compile_features(redesk_core PUBLIC cxx_std_20)

# Repo-root include so '#include "core/capture/capturer.h"' resolves everywhere;
# generated/ exposes redesk_version.h. PUBLIC so consumers (service/ui/tests)
# inherit the search paths by linking REDesk::core.
target_include_directories(redesk_core PUBLIC
    "${REDESK_REPO_ROOT}"
    "${REDESK_GENERATED_DIR}"
)

# Propagate the real-vs-stub switch as a compile definition so the guarded
# factory code (#if defined(REDESK_USE_REAL_BACKENDS)) compiles correctly.
# PUBLIC: downstream code may also branch on it.
if(REDESK_USE_REAL_BACKENDS)
    target_compile_definitions(redesk_core PUBLIC REDESK_USE_REAL_BACKENDS=1)
endif()

# core links only the common foundation + std. No OS/vendor deps in the stub
# build — that invariant is what keeps the portable CI build green.
target_link_libraries(redesk_core PUBLIC REDesk::common)

# Each layer contributes its interface header + stub .cpp to redesk_core.
add_subdirectory(capture)
add_subdirectory(codec)
add_subdirectory(transport)
add_subdirectory(crypto)
add_subdirectory(input)
add_subdirectory(session)
