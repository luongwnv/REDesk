# tests/ — REDesk scaffold test suite.
#
# ADR-001 §5 (tests/). Dependency-free: uses the in-repo redesk_test.h harness,
# NOT GoogleTest/Catch2, so `ctest` runs against the stub build on any host with
# no external test framework. Each test is a small standalone executable
# registered via add_test() so CTest drives the whole suite.
#
# These tests exercise the stub backends + the proto contract and prove the
# scaffold is wired correctly (they pass in the default REDESK_USE_REAL_BACKENDS=OFF
# build). See tests/stub_backends.h for the "EXPECTED CORE API" the reference
# stubs stand in for until core/codec, core/transport, core/crypto, and
# core/session land with confirmed signatures.

# ---------------------------------------------------------------------------
# Shared test-support INTERFACE target: propagates the repo-root include path
# (so "proto/..." , "tests/..." , and "core/common/types.h" resolve) and the
# generated/ dir (redesk_version.h), and links the shared value types from core.
# ---------------------------------------------------------------------------
add_library(redesk_test_support INTERFACE)
add_library(REDesk::test_support ALIAS redesk_test_support)

target_compile_features(redesk_test_support INTERFACE cxx_std_20)
target_include_directories(redesk_test_support INTERFACE
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_BINARY_DIR}/generated"
)

# Link REDesk::core for the shared types (Status/Result/VideoFrame/...). The core
# subdir is added before tests/ by the top-level CMakeLists, so the alias exists.
if(TARGET REDesk::core)
    target_link_libraries(redesk_test_support INTERFACE REDesk::core)
else()
    message(WARNING
        "REDesk::core target not found when configuring tests/. "
        "Tests still build against core/common/types.h via the repo-root include, "
        "but the integrator should ensure core/ is added before tests/.")
endif()

# The proto target is header-only. If the top-level CMakeLists does not yet
# add_subdirectory(proto) (the foundation tree lists core/platform/ui/service/
# server/tests/third_party), pull it in here so REDesk::proto is available to
# tests. INTEGRATOR: prefer adding `add_subdirectory(proto)` at the top level so
# core/, service/, and ui/ can link REDesk::proto too.
if(NOT TARGET REDesk::proto)
    if(EXISTS "${CMAKE_SOURCE_DIR}/proto/CMakeLists.txt")
        add_subdirectory("${CMAKE_SOURCE_DIR}/proto" "${CMAKE_BINARY_DIR}/proto")
    endif()
endif()
if(TARGET REDesk::proto)
    target_link_libraries(redesk_test_support INTERFACE REDesk::proto)
endif()

# ---------------------------------------------------------------------------
# Helper: declare one test executable + register it with CTest.
# ---------------------------------------------------------------------------
function(redesk_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE REDesk::test_support)
    set_target_properties(${name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/tests")
    add_test(NAME ${name} COMMAND ${name})
    # Fail the test if the harness prints a failure summary even if the process
    # somehow returns 0 (belt-and-suspenders for CI).
    set_tests_properties(${name} PROPERTIES
        FAIL_REGULAR_EXPRESSION "FAILED|FAILURE")
endfunction()

# ---------------------------------------------------------------------------
# The suite (each proves a scaffold seam against the stubs / proto contract).
# ---------------------------------------------------------------------------
redesk_add_test(test_proto_framing)        # wire framing + 65535 fragmentation
redesk_add_test(test_codec_negotiation)    # negotiateCodec picks highest common tier
redesk_add_test(test_codec_roundtrip)      # stub encoder -> decoder is equivalent
redesk_add_test(test_transport_loopback)   # loopback delivery + reliability semantics
redesk_add_test(test_crypto_handshake)     # stub Noise handshake + AEAD round-trip
redesk_add_test(test_session_permissions)  # default-deny gate + fingerprint audit log
