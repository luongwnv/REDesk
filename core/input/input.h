#pragma once

// Input injection + clipboard sync abstraction (ADR-001 §3.4).
//
// IInputInjector unifies the three OS event models: SendInput (Windows),
// CGEvent (macOS), portal+libei / wlroots / XTEST (Linux, selected at RUNTIME by
// capability probe — never compile-time, §3.4). The interface deliberately
// separates the channels that real backends keep separate:
//   * absolute vs relative pointer motion (DDA virtual-desktop normalization is
//     a Windows backend detail; this interface takes normalized [0,1] for
//     absolute and raw deltas for relative);
//   * HID SCANCODE keys vs a separate UNICODE text path (§3.4: scancode +
//     KEYEVENTF_UNICODE / CGEventKeyboardSetUnicodeString are distinct);
//   * explicit modifier set/reset, because held-modifier chords are fragile on
//     macOS CGEventSetFlags and desync on wlroots per-char remap (§3.4).
//
// IClipboardSync handles text/image/uri-list get/set + a change callback. File
// CONTENTS are streamed over the transport and synthesized as virtual files on
// paste (§3.4) — that streaming is a session/transport concern; this interface
// covers the local clipboard surface only.
//
// The default STUB (REDESK_USE_REAL_BACKENDS=OFF) injects nothing into the OS;
// it records every event into an in-memory log that tests inspect, and keeps an
// in-memory clipboard.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/common/types.h"

namespace redesk::input {

// Pointer buttons. Extra buttons map to backend-specific codes.
enum class MouseButton {
    Left,
    Right,
    Middle,
    Back,
    Forward,
};

enum class ButtonAction {
    Down,
    Up,
};

// Keyboard modifier bitflags (set/reset as a group, §3.4).
enum class Modifier : uint32_t {
    None  = 0,
    Shift = 1u << 0,
    Ctrl  = 1u << 1,
    Alt   = 1u << 2,   // Option on macOS
    Meta  = 1u << 3,   // Win / Command / Super
};

inline Modifier operator|(Modifier a, Modifier b) {
    return static_cast<Modifier>(static_cast<uint32_t>(a) |
                                 static_cast<uint32_t>(b));
}
inline Modifier operator&(Modifier a, Modifier b) {
    return static_cast<Modifier>(static_cast<uint32_t>(a) &
                                 static_cast<uint32_t>(b));
}

// Abstract input injector. Backends gate every call on the session's
// InputInjection capability AND the OS permission (macOS Accessibility via
// CGPreflightPostEventAccess; Wayland portal consent) — the session layer is the
// authority, backends are the last line (§3.4 "rely on your own authz gate").
class IInputInjector {
public:
    virtual ~IInputInjector() = default;

    // Absolute pointer move. `nx`/`ny` are normalized [0,1] across the controlled
    // display's bounds; the Windows backend applies the SM_XVIRTUALSCREEN
    // negative-origin normalization (§3.4). `display_id` selects the target.
    virtual Status mouseMoveAbsolute(const std::string& display_id, double nx,
                                     double ny) = 0;

    // Relative pointer move by raw device deltas (for captured/relative mode).
    virtual Status mouseMoveRelative(int32_t dx, int32_t dy) = 0;

    virtual Status mouseButton(MouseButton button, ButtonAction action) = 0;

    // Hi-res scroll. Deltas are in fractional wheel units; backends quantize
    // (Windows 120-unit WHEEL_DELTA; macOS pixel vs line per target, §3.4).
    virtual Status scroll(double dx, double dy, bool hi_res) = 0;

    // HID scancode key (set 1 / "extended" handled by the backend). Use this for
    // game/shortcut fidelity; pair with setModifiers() for chords.
    virtual Status keyScancode(uint32_t scancode, ButtonAction action) = 0;

    // Type a single Unicode codepoint regardless of layout (text entry path).
    virtual Status keyUnicode(uint32_t codepoint, ButtonAction action) = 0;

    // Set the held modifier set explicitly (down/up the delta). Prefer this over
    // relying on per-event flags for chords (§3.4 macOS/ wlroots corrections).
    virtual Status setModifiers(Modifier modifiers) = 0;

    // Release all held keys/buttons/modifiers — call on session end / focus loss
    // so the controlled machine is never left with a stuck key.
    virtual Status reset() = 0;
};

// Clipboard payload kinds (§3.4). Image is raw bytes + a format hint; uri-list
// is RFC 2483 text/uri-list (file paths are NEVER shared raw — file CONTENTS go
// over the transport, §3.4).
enum class ClipboardKind {
    Text,
    Image,
    UriList,
};

struct ClipboardData {
    ClipboardKind kind = ClipboardKind::Text;
    std::string text;                 // Text or uri-list (newline-separated)
    std::vector<uint8_t> image_bytes; // Image only (PNG/DIB; see image_mime)
    std::string image_mime;           // e.g. "image/png"
};

using ClipboardChangeCallback = std::function<void(const ClipboardData&)>;

class IClipboardSync {
public:
    virtual ~IClipboardSync() = default;

    virtual Result<ClipboardData> get(ClipboardKind kind) = 0;
    virtual Status set(const ClipboardData& data) = 0;

    // Notified when the LOCAL clipboard changes (so the session can mirror it to
    // the peer). Backends debounce/own-write-suppress to avoid sync loops.
    virtual void setChangeCallback(ClipboardChangeCallback cb) = 0;
};

// Factories. Real backend (runtime-probed per §3.4) when
// REDESK_USE_REAL_BACKENDS=ON, else the in-memory recording stub.
std::unique_ptr<IInputInjector> createInputInjector();
std::unique_ptr<IClipboardSync> createClipboardSync();

// ---------------------------------------------------------------------------
// Test-only: the stub injector exposes its recorded event log so tests can
// assert exactly what would have been injected.
// ---------------------------------------------------------------------------
struct RecordedEvent {
    enum class Type {
        MouseMoveAbs,
        MouseMoveRel,
        MouseButton,
        Scroll,
        KeyScancode,
        KeyUnicode,
        SetModifiers,
        Reset,
    } type;
    // Loosely-typed fields; only the relevant ones are set per Type.
    std::string display_id;
    double nx = 0, ny = 0;
    int32_t dx = 0, dy = 0;
    double sdx = 0, sdy = 0;
    bool hi_res = false;
    MouseButton button = MouseButton::Left;
    ButtonAction action = ButtonAction::Down;
    uint32_t code = 0;       // scancode or codepoint
    Modifier modifiers = Modifier::None;
};

class IStubInputInjector : public IInputInjector {
public:
    virtual const std::vector<RecordedEvent>& events() const = 0;
    virtual void clear() = 0;
};

std::unique_ptr<IStubInputInjector> createStubInputInjector();

} // namespace redesk::input
