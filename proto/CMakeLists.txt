# proto/ — REDesk wire + IPC protocol (single source of truth).
#
# Header-only INTERFACE library. Dependency-free by design (ADR §3.3/§3.5/§3.6):
# the stub build pulls in no protobuf/gRPC/Qt. Consumers link REDesk::proto and
# include with repo-root-relative paths, e.g. #include "proto/ipc.proto.h".

add_library(redesk_proto INTERFACE)
add_library(REDesk::proto ALIAS redesk_proto)

target_compile_features(redesk_proto INTERFACE cxx_std_20)

# Repo-root include so "proto/..." and "core/common/types.h" resolve, plus the
# generated/ dir for redesk_version.h. We add these directly (rather than relying
# solely on a transitive dep) so proto stays usable even if linked in isolation.
target_include_directories(redesk_proto INTERFACE
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_BINARY_DIR}/generated"
)

# proto reuses the shared value types (Status/Size/Rect/PixelFormat/ErrorCode)
# from core/common/types.h. Link the core target for those + its include paths.
# REDesk::core is defined in core/ (added before proto consumers in the tree).
if(TARGET REDesk::core)
    target_link_libraries(redesk_proto INTERFACE REDesk::core)
endif()

# Make the headers visible to IDEs / install tooling.
target_sources(redesk_proto INTERFACE
    FILE_SET redesk_proto_headers TYPE HEADERS
    BASE_DIRS "${CMAKE_SOURCE_DIR}"
    FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/transport.proto.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/codec.proto.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/ipc.proto.h"
)
