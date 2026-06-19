// platform/macos/stub_macos.cpp — portable null factories for the DEFAULT
// macOS build (ADR-001 §3.1/§3.2/§3.4/§3.6).
//
// This plain-C++ translation unit is the ONLY macOS source compiled when
// REDESK_USE_REAL_BACKENDS=OFF. It carries no ObjC++/framework dependency so a
// stock macOS + clang toolchain (no Apple media entitlements) configures and
// builds cleanly. The real backends live in the sibling .mm files, which are
// added to the build only on (APPLE AND REDESK_USE_REAL_BACKENDS) — so exactly
// one definition of each CreateNative*() exists per configuration.

#include "platform/platform_backends.h"

#if !REDESK_USE_REAL_BACKENDS

namespace redesk::platform {

std::unique_ptr<redesk::capture::ICapturer> CreateNativeCapturer() {
    return nullptr;  // core stub capturer is used
}
std::unique_ptr<redesk::codec::IVideoEncoder> CreateNativeVideoEncoder() {
    return nullptr;
}
std::unique_ptr<redesk::input::IInputInjector> CreateNativeInputInjector() {
    return nullptr;
}
std::unique_ptr<redesk::crypto::IKeyStore> CreateNativeKeyStore() {
    return nullptr;
}

} // namespace redesk::platform

#endif // !REDESK_USE_REAL_BACKENDS
