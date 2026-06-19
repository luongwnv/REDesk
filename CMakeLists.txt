cmake_minimum_required(VERSION 3.21)

# Objective-C++ is needed for the macOS native backends (ScreenCaptureKit,
# VideoToolbox, CGEvent, Keychain) when REDESK_USE_REAL_BACKENDS=ON. Declaring it
# at project() scope ensures CMAKE_OBJCXX_COMPILE_OBJECT is configured before any
# enable_language(OBJCXX) in platform/macos.
if(APPLE)
    set(_REDESK_LANGS CXX OBJCXX)
else()
    set(_REDESK_LANGS CXX)
endif()

project(REDesk
    VERSION 0.1.0
    DESCRIPTION "Native cross-platform remote desktop (Windows/macOS/Linux), AnyDesk-class smoothness"
    LANGUAGES ${_REDESK_LANGS}
)

# ---------------------------------------------------------------------------
# Global settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# Always export compile_commands.json for tooling/IDEs.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# ---------------------------------------------------------------------------
# Detect host platform -> REDESK_PLATFORM (drives platform/ backend selection)
# ---------------------------------------------------------------------------
if(WIN32)
    set(REDESK_PLATFORM "windows")
elseif(APPLE)
    set(REDESK_PLATFORM "macos")
elseif(UNIX)
    set(REDESK_PLATFORM "linux")
else()
    message(FATAL_ERROR "REDesk: unsupported platform")
endif()
message(STATUS "REDesk platform backend: ${REDESK_PLATFORM}")

# ---------------------------------------------------------------------------
# Options & feature toggles (see cmake/REDeskOptions.cmake)
# ---------------------------------------------------------------------------
include(REDeskOptions)

# Generated version header (core/redesk_version.h is git-ignored).
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/redesk_version.h.in"
    "${CMAKE_BINARY_DIR}/generated/redesk_version.h"
    @ONLY
)

# ---------------------------------------------------------------------------
# Subdirectories
# ---------------------------------------------------------------------------
add_subdirectory(third_party)
add_subdirectory(core)
add_subdirectory(platform)

if(REDESK_BUILD_UI)
    add_subdirectory(ui)
endif()
if(REDESK_BUILD_SERVICE)
    add_subdirectory(service)
endif()
if(REDESK_BUILD_SERVERS)
    add_subdirectory(server)
endif()
if(REDESK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Packaging / deployment (macdeployqt / windeployqt / AppImage + CPack). No-op
# unless the UI target was built. Must come after ui/ so redesk_ui exists.
include(PackageDeploy)

# ---------------------------------------------------------------------------
# Configuration summary
# ---------------------------------------------------------------------------
message(STATUS "------------------------------------------------------------")
message(STATUS "REDesk ${PROJECT_VERSION} configuration")
message(STATUS "  Platform backend : ${REDESK_PLATFORM}")
message(STATUS "  UI client        : ${REDESK_BUILD_UI}")
message(STATUS "  Service daemon   : ${REDESK_BUILD_SERVICE}")
message(STATUS "  Servers          : ${REDESK_BUILD_SERVERS}")
message(STATUS "  Tests            : ${REDESK_BUILD_TESTS}")
message(STATUS "  Real backends    : ${REDESK_USE_REAL_BACKENDS} (OFF = stub/portable build)")
message(STATUS "------------------------------------------------------------")
