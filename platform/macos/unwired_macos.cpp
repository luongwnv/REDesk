// platform/macos/unwired_macos.cpp — temporary null factories for the macOS
// backends not yet reconciled with the real core interfaces (ADR-001 §3.2/§3.4/
// §3.6: encoder, input, keystore).
//
// Phase 1 wires only the capturer (capturer_sck.mm). Until the encoder/input/
// keystore .mm files are reconciled against the real core headers, this TU
// supplies their null CreateNative*() so the real-backend build links. It is
// compiled only on the real-backend macOS path (the OFF path uses stub_macos.cpp).
//
// NOTE: CreateNativeCapturer() is intentionally NOT defined here — the real one
// lives in capturer_sck.mm.

#include "platform/platform_backends.h"

#if REDESK_USE_REAL_BACKENDS

namespace redesk::platform {

std::unique_ptr<redesk::codec::IVideoEncoder> CreateNativeVideoEncoder() {
    return nullptr;  // TODO(Phase 1+): reconcile + enable encoder_videotoolbox.mm
}
std::unique_ptr<redesk::input::IInputInjector> CreateNativeInputInjector() {
    return nullptr;  // TODO(Phase 1+): reconcile + enable input_cgevent.mm
}
std::unique_ptr<redesk::crypto::IKeyStore> CreateNativeKeyStore() {
    return nullptr;  // TODO(Phase 1+): reconcile + enable keystore_keychain.mm
}

}  // namespace redesk::platform

#endif  // REDESK_USE_REAL_BACKENDS
