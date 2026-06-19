#pragma once

// REDesk local IPC protocol — UI <-> service RPC contract.
//
// ADR-001 §3.5 (UI / two-process split) and §3.6 #6 (permission model).
//
// The UI client (unprivileged, Qt Quick) talks to the privileged service over
// an authenticated, ACL-restricted local channel (named pipe on Windows / Unix
// domain socket elsewhere — ADR §3.5). This header defines that RPC contract as
// plain POD structs plus a discriminated-union-style IpcMessageType tag. It is
// dependency-free on purpose (no protobuf/Qt) so it can be shared verbatim by
// service/ and ui/ in the stub build; a protobuf/QDataStream codec can later be
// generated from these shapes.
//
// SECURITY (ADR §3.5 correction): the UI is unprivileged and impersonable. The
// service MUST authenticate the channel and validate EVERY message as untrusted.
// These structs describe shape only — they carry no implicit trust.
//
// Capabilities map 1:1 to the ADR §3.6 #6 default-deny permission toggles.

#include <cstdint>
#include <string>
#include <vector>

#include "proto/codec.proto.h"
#include "proto/transport.proto.h"

namespace redesk::proto {

// ---------------------------------------------------------------------------
// Per-session capabilities (ADR §3.6 #6). Default-deny: a capability is in
// force only if explicitly granted for the session. Bit positions are part of
// the IPC contract — append only.
// ---------------------------------------------------------------------------
enum class Capability : uint32_t {
    kViewScreen   = 1u << 0,  // see the remote screen (view-only floor)
    kControlInput = 1u << 1,  // inject mouse/keyboard
    kClipboard    = 1u << 2,  // clipboard sync
    kFileTransfer = 1u << 3,  // file transfer
    kAudio        = 1u << 4,  // audio capture/playback
    kPrivacyMode  = 1u << 5,  // privacy / black-screen on host
    kUacElevation = 1u << 6,  // drive elevated / Secure Desktop (ADR §3.4)
};

inline constexpr uint32_t kNoCapabilities = 0;

// A set of capabilities as an OR-able bitmask wrapper. Kept tiny and POD-ish.
struct CapabilitySet {
    uint32_t bits = kNoCapabilities;

    bool has(Capability c) const { return (bits & static_cast<uint32_t>(c)) != 0; }
    void grant(Capability c) { bits |= static_cast<uint32_t>(c); }
    void revoke(Capability c) { bits &= ~static_cast<uint32_t>(c); }
    static CapabilitySet none() { return {}; }
};

// ---------------------------------------------------------------------------
// Session lifecycle state surfaced from service to UI.
// ---------------------------------------------------------------------------
enum class SessionPhase : uint8_t {
    kIdle           = 0,
    kSignaling      = 1,  // talking to rendezvous, brokering ICE
    kHandshaking    = 2,  // Noise XK/KK in progress (ADR §3.6)
    kAuthenticating = 3,  // PIN/CPace or password/OPAQUE inside the channel
    kConnected      = 4,
    kDisconnected   = 5,
    kFailed         = 6,
};

// Roles for the local machine in a session.
enum class SessionRole : uint8_t {
    kController = 0,  // viewer (drives the remote)
    kHost      = 1,   // controlled (being viewed/driven)
};

// ---------------------------------------------------------------------------
// IPC message discriminator. Every IPC frame begins with one of these tags;
// the body is the matching struct below. Values are append-only.
// ---------------------------------------------------------------------------
enum class IpcMessageType : uint16_t {
    kUnknown = 0,

    // --- Requests (UI -> service) ---
    kGetMyId          = 10,  // ask for this device's REDesk ID + fingerprint
    kConnectToPeer    = 11,  // start an outgoing session
    kAcceptIncoming   = 12,  // host accepts an inbound request (consent dialog)
    kRejectIncoming   = 13,
    kDisconnect       = 14,
    kSetCapabilities  = 15,  // host toggles per-session permission grants
    kRequestKeyframe  = 16,  // UI asks service to request an IDR from peer
    kSendInputEvent   = 17,  // UI forwards a local input event to inject remotely
    kListDisplays     = 18,

    // --- Responses / events (service -> UI) ---
    kMyId             = 110,
    kSessionState     = 111,  // phase/role transitions, errors
    kIncomingRequest  = 112,  // inbound session awaiting consent
    kCapabilityGrant  = 113,  // authoritative capability set now in force
    kFingerprintAlert = 114,  // TOFU key-change / verification prompt (ADR §3.6 #3)
    kDisplayList      = 115,
    kInputEvent       = 116,  // passthrough of a remote input event toward injector
    kError            = 199,
};

// ---------------------------------------------------------------------------
// Request bodies (UI -> service).
// ---------------------------------------------------------------------------
struct GetMyIdRequest {
    // No fields; the tag alone is the request.
};

struct ConnectToPeerRequest {
    std::string peer_id;            // REDesk routing handle (NOT the identity)
    std::string pin;                // one-time interactive PIN (CPace input)
    std::string unattended_password; // optional; empty for attended sessions
    CapabilitySet desired;          // capabilities the controller will request
    std::vector<CodecTier> codecs = floorCapabilities(); // probed local tiers
};

struct AcceptIncomingRequest {
    std::string session_id;
    CapabilitySet granted;          // host's authoritative grant (default-deny)
};

struct RejectIncomingRequest {
    std::string session_id;
    std::string reason;
};

struct DisconnectRequest {
    std::string session_id;
};

struct SetCapabilitiesRequest {
    std::string session_id;
    CapabilitySet granted;          // new authoritative set (replaces prior)
};

struct RequestKeyframeRequest {
    std::string session_id;
};

// A single input event passed through the IPC boundary toward the injector.
// Deliberately codec-agnostic and OS-agnostic; core/input maps it to the native
// backend. Mirrors the kinds called out in ADR §3.4.
enum class InputEventKind : uint8_t {
    kMouseMoveAbs   = 0,  // absolute, normalized 0..65535 (virtual desktop)
    kMouseMoveRel   = 1,  // relative delta
    kMouseButton    = 2,
    kMouseScroll    = 3,  // hi-res scroll, 120-unit deltas
    kKeyScancode    = 4,  // HID scancode key (with extended flag)
    kKeyUnicode     = 5,  // separate Unicode text path
    kModifierSet    = 6,  // explicit modifier set/reset
};

struct InputEvent {
    InputEventKind kind = InputEventKind::kMouseMoveAbs;
    int32_t  x = 0;          // abs (normalized) or relative dx, by kind
    int32_t  y = 0;
    int32_t  button = 0;     // button index, scroll axis, or modifier mask
    int32_t  delta = 0;      // scroll delta / scancode / unicode codepoint
    bool     pressed = false; // down vs up for button/key events
    bool     extended = false;// KEYEVENTF_EXTENDEDKEY analogue
};

struct SendInputEventRequest {
    std::string session_id;
    InputEvent  event;
};

struct ListDisplaysRequest {
    std::string session_id;
};

// ---------------------------------------------------------------------------
// Response / event bodies (service -> UI).
// ---------------------------------------------------------------------------
struct MyIdResponse {
    std::string redesk_id;          // routing handle
    std::string fingerprint;        // BLAKE2b safety number = the real identity
};

struct SessionStateEvent {
    std::string  session_id;
    SessionRole  role  = SessionRole::kController;
    SessionPhase phase = SessionPhase::kIdle;
    CodecTier    negotiated_codec = CodecTier::kNone;
    std::string  detail;            // human-readable status / error context
};

struct IncomingRequestEvent {
    std::string session_id;
    std::string peer_id;
    std::string peer_fingerprint;   // for the consent dialog / OOB compare
    CapabilitySet requested;        // what the controller asked for
};

// Authoritative capability set now in force for a session (default-deny result
// of the host's grant). The UI reflects toggle state from this — it never
// assumes a capability is on.
struct CapabilityGrantEvent {
    std::string   session_id;
    CapabilitySet granted;
};

struct FingerprintAlertEvent {
    std::string peer_id;
    std::string old_fingerprint;    // empty on first contact (TOFU)
    std::string new_fingerprint;
    bool        is_key_change = false; // true => potential MITM, warn loudly
};

struct DisplayInfo {
    uint32_t id = 0;
    Size     size;                  // from core/common/types.h
    bool     is_primary = false;
    double   scale = 1.0;           // Retina / high-DPI scale factor
};

struct DisplayListResponse {
    std::string              session_id;
    std::vector<DisplayInfo> displays;
};

struct InputEventPassthrough {
    std::string session_id;
    InputEvent  event;
};

struct ErrorResponse {
    ErrorCode   code = ErrorCode::Internal; // from core/common/types.h
    std::string message;
};

// ---------------------------------------------------------------------------
// Envelope. A concrete IPC codec (QDataStream / protobuf, generated later) is
// responsible for tag-dispatching to the right body. This struct documents the
// header that precedes every body on the local channel.
// ---------------------------------------------------------------------------
#pragma pack(push, 1)
struct IpcHeader {
    uint16_t       version = kProtocolVersion;
    IpcMessageType type    = IpcMessageType::kUnknown;
    uint32_t       length  = 0;  // body byte length following the header
};
#pragma pack(pop)

static_assert(sizeof(IpcHeader) == 8,
              "IpcHeader wire size changed — bump kProtocolVersion and update peers");

} // namespace redesk::proto
