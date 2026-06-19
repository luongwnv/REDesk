// platform/windows/input_sendinput.cpp — Windows input injection backend.
// ADR-001 §3.4 (Input Injection, Windows): one SendInput() per event batch.
//
// ADR corrections this backend MUST honor (see TODOs):
//   * Absolute mouse: MOUSEEVENTF_MOVE|ABSOLUTE|VIRTUALDESK normalized with the
//     NEGATIVE-ORIGIN offset:
//       x = round((x - SM_XVIRTUALSCREEN) * 65535 / (SM_CXVIRTUALSCREEN - 1))
//       y = round((y - SM_YVIRTUALSCREEN) * 65535 / (SM_CYVIRTUALSCREEN - 1))
//     Verify the cursor reaches the extreme bottom-right pixel and monitors
//     left/above the primary.
//   * Keyboard: KEYEVENTF_SCANCODE (+EXTENDEDKEY for the extended set); arbitrary
//     text via KEYEVENTF_UNICODE on a SEPARATE Unicode path. Scroll in 120-unit
//     deltas (WHEEL_DELTA), hi-res supported.
//   * Privilege claim is SPLIT:
//       (1) elevated + uiAccess="true" + Authenticode-signed + TRUSTED install
//           path (Program Files / system32) -> SendInput can drive higher-
//           integrity NORMAL windows.
//       (2) The Secure Desktop (UAC consent, logon, lock) is reachable ONLY from
//           a SYSTEM session-0 service that OpenInputDesktop/SetThreadDesktop to
//           the current input desktop on WTS session-change / desktop-switch.
//           uiAccess ALONE does NOT reach the Secure Desktop.
//
// This TU also hosts the named-pipe ACL helper implementation (ADR §3.5),
// co-located because both are Win32 security/desktop plumbing.

#include "platform/platform_backends.h"
#include "platform/windows/named_pipe_acl.h"

#if REDESK_USE_REAL_BACKENDS

#include "core/input/injector.h"  // redesk::input::IInputInjector (core slice)

#include <windows.h>
#include <wtsapi32.h>  // WTSRegisterSessionNotification (desktop-hop trigger)

namespace redesk::platform {
namespace {

class SendInputInjector final : public redesk::input::IInputInjector {
public:
    SendInputInjector() = default;
    ~SendInputInjector() override = default;

    redesk::Status MoveAbsolute(double x_norm, double y_norm) override {
        (void)x_norm; (void)y_norm;
        // TODO(ADR §3.4): map normalized [0,1] target into virtual-desktop
        // coordinates, then apply the negative-origin 0..65535 normalization
        // above with MOUSEEVENTF_VIRTUALDESK|ABSOLUTE|MOVE. Single SendInput.
        return Unsupported();
    }

    redesk::Status MoveRelative(int32_t dx, int32_t dy) override {
        (void)dx; (void)dy;
        // TODO(ADR §3.4): MOUSEEVENTF_MOVE (relative); used for raw/relative
        // pointer mode (games) where ABSOLUTE warps are undesirable.
        return Unsupported();
    }

    redesk::Status Button(redesk::input::MouseButton b, bool down) override {
        (void)b; (void)down;
        // TODO(ADR §3.4): MOUSEEVENTF_{LEFT,RIGHT,MIDDLE,X}{DOWN,UP}.
        return Unsupported();
    }

    redesk::Status Scroll(int32_t dx_120, int32_t dy_120) override {
        (void)dx_120; (void)dy_120;
        // TODO(ADR §3.4): MOUSEEVENTF_WHEEL / MOUSEEVENTF_HWHEEL in 120-unit
        // (WHEEL_DELTA) increments; pass hi-res deltas through unscaled.
        return Unsupported();
    }

    redesk::Status KeyScancode(uint16_t scancode, bool extended, bool down) override {
        (void)scancode; (void)extended; (void)down;
        // TODO(ADR §3.4): KEYEVENTF_SCANCODE (| KEYEVENTF_EXTENDEDKEY) (| KEYUP).
        return Unsupported();
    }

    redesk::Status TypeUnicode(const std::u16string& text) override {
        (void)text;
        // TODO(ADR §3.4): separate Unicode path — one KEYEVENTF_UNICODE
        // down/up pair per UTF-16 code unit (surrogate pairs as two units).
        return Unsupported();
    }

    redesk::Status ResetModifiers() override {
        // TODO(ADR §3.4): force key-up for all tracked modifiers to avoid stuck
        // chords across reconnects / focus changes.
        return Unsupported();
    }

private:
    static redesk::Status Unsupported() {
        return redesk::Status::error(redesk::ErrorCode::Unsupported,
                                     "SendInput injection not implemented");
    }
    // TODO(ADR §3.4, Secure Desktop): when running as the SYSTEM session-0
    // service, register WTSRegisterSessionNotification + a desktop-switch hook;
    // on WTS_SESSION_* / desktop change call OpenInputDesktop + SetThreadDesktop
    // to follow the active input desktop (Secure Desktop / Winlogon / Default).
    // uiAccess alone is insufficient for the Secure Desktop.
};

} // namespace

std::unique_ptr<redesk::input::IInputInjector> CreateNativeInputInjector() {
    return std::make_unique<SendInputInjector>();
}

} // namespace redesk::platform

// ---- named-pipe ACL helper (ADR §3.5) -------------------------------------
namespace redesk::platform::win {

redesk::Result<PipeSecurity> BuildServicePipeSecurity(const PipeAclPolicy& policy) {
    (void)policy;
    // TODO(ADR §3.5): build an explicit, protected DACL:
    //   ALLOW SYSTEM / Administrators / policy.extra_user_sid (GENERIC_RW),
    //   DENY  Everyone (S-1-1-0) + anonymous,
    //   SE_DACL_PROTECTED (no inheritance), wrap in SECURITY_ATTRIBUTES.
    return redesk::Result<PipeSecurity>::fail(
        redesk::ErrorCode::Unsupported, "BuildServicePipeSecurity not implemented");
}

void ReleaseServicePipeSecurity(PipeSecurity& sec) {
    // TODO(ADR §3.5): LocalFree the descriptor; zero the struct.
    sec.security_attributes = nullptr;
    sec.security_descriptor = nullptr;
}

} // namespace redesk::platform::win

#else // !REDESK_USE_REAL_BACKENDS

namespace redesk::platform {
std::unique_ptr<redesk::input::IInputInjector> CreateNativeInputInjector() {
    return nullptr;
}
} // namespace redesk::platform

namespace redesk::platform::win {
redesk::Result<PipeSecurity> BuildServicePipeSecurity(const PipeAclPolicy&) {
    return redesk::Result<PipeSecurity>::fail(
        redesk::ErrorCode::Unsupported, "named-pipe ACL unavailable in stub build");
}
void ReleaseServicePipeSecurity(PipeSecurity& sec) {
    sec.security_attributes = nullptr;
    sec.security_descriptor = nullptr;
}
} // namespace redesk::platform::win

#endif // REDESK_USE_REAL_BACKENDS
