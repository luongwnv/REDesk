// platform/macos/input_cgevent.mm — macOS input injection backend.
// ADR-001 §3.4 (Input Injection, macOS): Quartz CGEventCreate* + CGEventPost.
//
// Compiled only on (APPLE AND REDESK_USE_REAL_BACKENDS).
//
// ADR corrections this backend MUST honor (see TODOs):
//   * Scroll: CGEventCreateScrollWheelEvent2 — pixel units for smooth,
//     line units for legacy / some Electron/AppKit targets.
//   * Unicode: CGEventKeyboardSetUnicodeString (separate from scancode path).
//   * Modifiers: do NOT rely solely on CGEventSetFlags + kCGHIDEventTap for
//     held-modifier chords (it drops flags in some cases). Either post explicit
//     modifier key-down/up around the chord, or use kCGSessionEventTap when flag
//     fidelity matters; reserve kCGHIDEventTap for physical-device semantics
//     (games/fullscreen). Test Cmd+Tab, Shift+drag on Sequoia/Tahoe.
//   * Permission: Accessibility TCC; gate at runtime with
//     CGPreflightPostEventAccess() — do NOT trust AXIsProcessTrusted. Ship the
//     injector in a normally-launched / SMAppService helper (Sequoia
//     background-helper regression). Distribute Developer ID, not MAS (sandbox
//     breaks PID taps).

#include "platform/platform_backends.h"

#if REDESK_USE_REAL_BACKENDS

#include "core/input/injector.h"  // redesk::input::IInputInjector (core slice)

#import <CoreGraphics/CoreGraphics.h>

namespace redesk::platform {
namespace {

class CgEventInjector final : public redesk::input::IInputInjector {
public:
    CgEventInjector() {
        // TODO(ADR §3.4): cache CGPreflightPostEventAccess() result; expose a
        // "needs Accessibility grant" signal to the UI when false. Do NOT call
        // AXIsProcessTrusted as the gate.
    }
    ~CgEventInjector() override = default;

    redesk::Status MoveAbsolute(double x_norm, double y_norm) override {
        (void)x_norm; (void)y_norm;
        // TODO(ADR §3.4): map normalized target to global CG coordinates across
        // the display arrangement; CGEventCreateMouseEvent(kCGEventMouseMoved)
        // + CGEventPost(tap). Use kCGSessionEventTap by default.
        return Unsupported();
    }

    redesk::Status MoveRelative(int32_t dx, int32_t dy) override {
        (void)dx; (void)dy;
        // TODO(ADR §3.4): track a synthetic cursor position and post moves;
        // CGEvent has no native relative-move, so accumulate + clamp to bounds.
        return Unsupported();
    }

    redesk::Status Button(redesk::input::MouseButton b, bool down) override {
        (void)b; (void)down;
        // TODO(ADR §3.4): CGEventCreateMouseEvent with the matching
        // kCGEventLeft/Right/OtherMouse{Down,Up}; maintain click-count for
        // double/triple-click via kCGMouseEventClickState.
        return Unsupported();
    }

    redesk::Status Scroll(int32_t dx_120, int32_t dy_120) override {
        (void)dx_120; (void)dy_120;
        // TODO(ADR §3.4): CGEventCreateScrollWheelEvent2 — pixel units for
        // smooth scroll; switch to line units for legacy/Electron targets.
        return Unsupported();
    }

    redesk::Status KeyScancode(uint16_t scancode, bool extended, bool down) override {
        (void)scancode; (void)extended; (void)down;
        // TODO(ADR §3.4): translate the wire HID scancode to a macOS virtual
        // keycode; CGEventCreateKeyboardEvent(keycode, down). For chords, post
        // explicit modifier key-down/up rather than relying on CGEventSetFlags.
        return Unsupported();
    }

    redesk::Status TypeUnicode(const std::u16string& text) override {
        (void)text;
        // TODO(ADR §3.4): per code unit, CGEventCreateKeyboardEvent(0,true) then
        // CGEventKeyboardSetUnicodeString(...), post down+up.
        return Unsupported();
    }

    redesk::Status ResetModifiers() override {
        // TODO(ADR §3.4): post key-up for all tracked modifier keycodes to clear
        // stuck chords (Cmd/Shift/Option/Control) across reconnects.
        return Unsupported();
    }

private:
    static redesk::Status Unsupported() {
        return redesk::Status::error(redesk::ErrorCode::Unsupported,
                                     "CGEvent injection not implemented");
    }
};

} // namespace

std::unique_ptr<redesk::input::IInputInjector> CreateNativeInputInjector() {
    return std::make_unique<CgEventInjector>();
}

} // namespace redesk::platform

#endif // REDESK_USE_REAL_BACKENDS
