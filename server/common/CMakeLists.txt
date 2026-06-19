# Shared server support library (ADR-001 §3.3).
#
# A tiny, dependency-free BSD-socket wrapper used by both redesk-rendezvous and
# redesk-relay. Stub-build-clean: std + OS sockets only. Winsock vs POSIX is
# guarded inside net.cpp; on Windows we link Ws2_32 (handled via #pragma comment
# there, and explicitly below for non-MSVC Windows toolchains).

add_library(redesk_server_net STATIC
    net.cpp
    net.h
)

target_link_libraries(redesk_server_net PUBLIC REDesk::core)

# Includes resolve relative to repo root: #include "server/common/net.h" and
# #include "core/common/types.h" both work.
target_include_directories(redesk_server_net PUBLIC
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_BINARY_DIR}/generated"
)

if(WIN32)
    target_link_libraries(redesk_server_net PUBLIC ws2_32)
endif()

add_library(REDesk::server_net ALIAS redesk_server_net)
