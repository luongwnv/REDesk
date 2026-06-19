# core/session sources -> folded into redesk_core (see core/CMakeLists.txt).
# This is the orchestration layer the service drives; it depends on core/crypto
# (for the peer fingerprint/identity types) which is part of the same target.
target_sources(redesk_core PRIVATE
    session.h
    session.cpp
)
