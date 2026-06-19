# core/crypto sources -> folded into redesk_core (see core/CMakeLists.txt).
#
# NOTE: crypto_stub.cpp is INSECURE-FOR-TESTS-ONLY (XOR cipher). When
# REDESK_USE_REAL_BACKENDS=ON the factories deliberately refuse to fall back to
# the stub Noise session (return nullptr) so it can never reach a shipping build.
target_sources(redesk_core PRIVATE
    crypto.h
    crypto_stub.cpp
)
