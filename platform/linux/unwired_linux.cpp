// platform/linux/unwired_linux.cpp — portable null factories for the default
// (stub) Linux build (ADR-001 §3.1/§3.2/§3.4/§3.6).
//
// The real Linux backends (capturer_pipewire / encoder_vaapi / input_ladder /
// keystore_libsecret) are built only on the real-backend path and still carry
// the original cross-slice header/signature guesses. Until they are reconciled
// with core, this TU supplies the four CreateNative*() returning nullptr.
//
// It includes the REAL core interface headers (capturer.h / codec.h / input.h /
// crypto.h) so the returned unique_ptr<T> has a complete T — GCC requires this
// to instantiate the deleter (clang is lenient, which is why this only surfaced
// on the Ubuntu/GCC build).

#include "platform/platform_backends.h"

#if !REDESK_USE_REAL_BACKENDS

#include "core/capture/capturer.h"
#include "core/codec/codec.h"
#include "core/crypto/crypto.h"
#include "core/input/input.h"

namespace redesk::platform {

std::unique_ptr<redesk::capture::ICapturer> CreateNativeCapturer() {
    return nullptr;  // core stub capturer is used (synthetic frames)
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

}  // namespace redesk::platform

#endif  // !REDESK_USE_REAL_BACKENDS
