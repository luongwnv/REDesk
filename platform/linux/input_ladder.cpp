// platform/linux/input_ladder.cpp — Linux input injection backend.
// ADR-001 §3.4 (Input Injection, Linux): a RUNTIME capability ladder, never
// compile-time:
//   RemoteDesktop portal + libei (ConnectToEIS)
//     -> wlroots virtual-pointer / virtual-keyboard
//       -> (Xorg) XTEST
//
// ADR corrections this backend MUST honor (see TODOs):
//   * Support floor: consumable portal ConnectToEIS + libei is GNOME >= 46 /
//     Mutter 46 (Xwayland 23.2) and KDE Plasma >= 6.1 — NOT "~GNOME 45." PROBE
//     ConnectToEIS availability; do NOT infer from compositor name/version.
//   * wlroots keyboard: upload ONE comprehensive xkb keymap ONCE at session
//     start — never per-character dynamic remap (desyncs the compositor keymap
//     -> invalid keycodes until a physical keypress: swaywm #2420, labwc #3113).
//     Prefer NotifyKeyboardKeysym / libei keysym injection where available.
//   * restore_token: write ATOMICALLY (temp + rename) immediately after each
//     successful Start, BEFORE sending input; treat re-consent as recoverable.
//   * Wayland injection has measurably higher latency than SendInput / CGEvent /
//     XTEST — surface "input unsupported on this compositor" gracefully; rely on
//     OUR OWN authz gate (wlroots no-consent protocols are unprivileged-but-
//     unsandboxed and may be disabled).

#include "platform/platform_backends.h"

#if REDESK_USE_REAL_BACKENDS

#include "core/input/injector.h"  // redesk::input::IInputInjector (core slice)

namespace redesk::platform {
namespace {

// Ladder rung chosen at runtime by probe (NOT compile-time).
enum class Backend { None, PortalLibei, WlrootsVirtual, XTest };

// Runtime probe: prefer portal+libei, else wlroots virtual protocols, else
// XTEST on Xorg. ConnectToEIS availability is queried directly, never inferred
// from compositor name/version (ADR §3.4 support-floor correction).
Backend ProbeBackend() {
    // TODO(ADR §3.4): in priority order —
    //   1. org.freedesktop.portal.RemoteDesktop present AND ConnectToEIS works
    //      (libei session establishes) -> Backend::PortalLibei.
    //   2. Wayland with zwlr_virtual_pointer_manager_v1 +
    //      zwp_virtual_keyboard_manager_v1 advertised -> Backend::WlrootsVirtual.
    //   3. $DISPLAY / Xorg with XTEST extension -> Backend::XTest.
    //   else None (surface "input unsupported on this compositor").
    return Backend::None;
}

class LinuxInputLadder final : public redesk::input::IInputInjector {
public:
    LinuxInputLadder() : backend_(ProbeBackend()) {}
    ~LinuxInputLadder() override = default;

    redesk::Status MoveAbsolute(double x_norm, double y_norm) override {
        (void)x_norm; (void)y_norm;
        // TODO(ADR §3.4): libei ei_device_pointer_motion_absolute / wlroots
        // zwlr_virtual_pointer motion_absolute / XTEST XTestFakeMotionEvent.
        return Unsupported();
    }

    redesk::Status MoveRelative(int32_t dx, int32_t dy) override {
        (void)dx; (void)dy;
        // TODO(ADR §3.4): libei relative motion / wlroots motion / XTEST
        // relative warp.
        return Unsupported();
    }

    redesk::Status Button(redesk::input::MouseButton b, bool down) override {
        (void)b; (void)down;
        // TODO(ADR §3.4): map to evdev BTN_* (libei/wlroots) or XTEST button.
        return Unsupported();
    }

    redesk::Status Scroll(int32_t dx_120, int32_t dy_120) override {
        (void)dx_120; (void)dy_120;
        // TODO(ADR §3.4): libei hi-res scroll / wlroots axis / XTEST buttons 4-7
        // (legacy). Convert 120-units to the backend's scroll metric.
        return Unsupported();
    }

    redesk::Status KeyScancode(uint16_t scancode, bool extended, bool down) override {
        (void)scancode; (void)extended; (void)down;
        // TODO(ADR §3.4): prefer keysym injection (NotifyKeyboardKeysym / libei
        // keysym) so we do NOT remap the keymap per character. For the wlroots
        // virtual-keyboard path, upload one comprehensive keymap once in Start
        // and inject keycodes against THAT keymap.
        return Unsupported();
    }

    redesk::Status TypeUnicode(const std::u16string& text) override {
        (void)text;
        // TODO(ADR §3.4): map each character to a keysym and inject via the
        // keysym path; never dynamically remap keycodes per character.
        return Unsupported();
    }

    redesk::Status ResetModifiers() override {
        // TODO(ADR §3.4): release all tracked modifier keys on the active rung.
        return Unsupported();
    }

private:
    redesk::Status Unsupported() const {
        return redesk::Status::error(
            redesk::ErrorCode::Unsupported,
            "Linux input ladder: no usable backend / not implemented");
    }
    Backend backend_ = Backend::None;
    // TODO(ADR §3.4): for the portal path, persist the restore_token ATOMICALLY
    // (temp + rename) right after a successful Start and before the first event.
};

} // namespace

std::unique_ptr<redesk::input::IInputInjector> CreateNativeInputInjector() {
    return std::make_unique<LinuxInputLadder>();
}

} // namespace redesk::platform

#else // !REDESK_USE_REAL_BACKENDS

namespace redesk::platform {
std::unique_ptr<redesk::input::IInputInjector> CreateNativeInputInjector() {
    return nullptr;
}
} // namespace redesk::platform

#endif // REDESK_USE_REAL_BACKENDS
