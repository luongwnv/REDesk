#pragma once

// platform/platform_backends.h — cross-layer factory contract for the native
// OS backends (ADR-001 §3.1 capture, §3.2 encode, §3.4 input, §3.6 keystore).
//
// The `core/` layer owns the abstract interfaces (ICapturer, IVideoEncoder,
// IInputInjector, IKeyStore) and ships portable STUB factories that are used by
// default. The `platform/` layer (this slice) supplies the REAL backends for
// the host OS. To avoid ODR clashes with core's stub factories, the native
// factories live in namespace `redesk::platform` with distinct names; the
// service is expected to prefer a native factory when it returns non-null and
// fall back to the core stub otherwise:
//
//     auto cap = redesk::platform::CreateNativeCapturer();   // null in stub build
//     if (!cap) cap = redesk::capture::CreateCapturer();     // core stub
//
// CROSS-SLICE ASSUMPTION (integrator: verify against the real core headers):
//   * core interface headers / namespaces are assumed to be:
//       core/capture/capturer.h    redesk::capture::ICapturer
//       core/codec/encoder.h       redesk::codec::IVideoEncoder
//       core/input/injector.h      redesk::input::IInputInjector
//       core/crypto/keystore.h     redesk::crypto::IKeyStore
//   * These headers are #included ONLY inside `#if REDESK_USE_REAL_BACKENDS`
//     blocks in the backend .cpp/.mm files, so the default stub build does not
//     depend on their final shape. If core lands these interfaces under
//     different paths/namespaces, only the guarded backend bodies need edits —
//     not this contract or the CMake wiring.
//
// In the default (REDESK_USE_REAL_BACKENDS=0) build, every CreateNative*()
// returns nullptr; the declarations below are still valid so callers compile
// against a stable surface regardless of the toggle.

#include <memory>

#include "core/common/types.h"

// Forward declarations only — we do NOT pull in the core interface headers here
// so this contract header stays cheap and the stub build never needs them.
namespace redesk::capture { class ICapturer; }
namespace redesk::codec   { class IVideoEncoder; }
namespace redesk::input   { class IInputInjector; }
namespace redesk::crypto  { class IKeyStore; }

namespace redesk::platform {

// Returns the host-OS native capturer (DXGI/SCK/PipeWire), or nullptr when
// real backends are disabled or unavailable on this host.
std::unique_ptr<redesk::capture::ICapturer> CreateNativeCapturer();

// Returns the host-OS native HW video encoder selector (NVENC/AMF/QSV/MF ·
// VideoToolbox · VAAPI/NVENC), or nullptr in the stub build. The returned
// object performs the ADR §3.2 test-encode probe before it advertises a tier.
std::unique_ptr<redesk::codec::IVideoEncoder> CreateNativeVideoEncoder();

// Returns the host-OS native input injector (SendInput · CGEvent · the Linux
// runtime capability ladder), or nullptr in the stub build.
std::unique_ptr<redesk::input::IInputInjector> CreateNativeInputInjector();

// Returns the host-OS native secret keystore (DPAPI-CNG · Keychain +
// SecureEnclave-wrapped · libsecret), or nullptr in the stub build.
std::unique_ptr<redesk::crypto::IKeyStore> CreateNativeKeyStore();

} // namespace redesk::platform
