# redesk_common — shared value types (types.h) + leveled logging.
#
# This is the foundation every other layer links. It is dependency-free (std
# only) so the REDESK_USE_REAL_BACKENDS=OFF stub build compiles on any host.
# types.h stays header-only; logging carries one small .cpp (default stderr
# sink), so the target is a tiny STATIC lib rather than INTERFACE.

add_library(redesk_common STATIC
    logging.cpp
    types.h        # listed for IDEs; header-only
    logging.h
)

add_library(REDesk::common ALIAS redesk_common)

target_compile_features(redesk_common PUBLIC cxx_std_20)

# Includes resolve relative to the repo root: #include "core/common/types.h".
# The build dir's generated/ is exposed so any consumer can pull redesk_version.h.
target_include_directories(redesk_common PUBLIC
    "${REDESK_REPO_ROOT}"
    "${REDESK_GENERATED_DIR}"
)
