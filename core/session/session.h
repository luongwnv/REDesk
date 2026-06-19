#pragma once

// Session orchestration: capability/permission model + lifecycle + audit
// (ADR-001 §3.6.6). This is the layer the service drives; it ties capture,
// codec, transport, crypto, and input together behind a permission gate.
//
// Permission model (§3.6.6): DEFAULT-DENY, least-privilege, explicit per-session
// capability grants — each a separate toggle (view-only vs control, clipboard,
// file transfer, audio, input injection, elevation). A persistent "being
// controlled" state is exposed for the UI indicator; a per-fingerprint audit log
// records every grant/deny/lifecycle event.
//
// Identity binding (§3.6.1/§3.6.3): a session is keyed by the PEER FINGERPRINT
// (the BLAKE2b safety number), not the REDesk routing ID. The audit log is keyed
// by fingerprint so allow/deny lists and key-change alerts have a stable subject.
//
// This header is dependency-free beyond core/common + core/crypto (for the
// fingerprint type). The SessionManager owns no real backends directly — the
// service injects capturer/encoder/transport/injector; the manager enforces the
// gate. Stub: a fully in-memory manager usable in tests with no real backends.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/common/types.h"
#include "core/crypto/crypto.h"

namespace redesk::session {

// Per-session capability bitflags (§3.6.6). Each is an independent toggle the
// controlled side grants. ViewOnly is the implicit floor; Control implies the
// peer may steer but still needs InputInjection to actually inject events.
enum class Capabilities : uint32_t {
    None           = 0,
    ViewOnly       = 1u << 0,  // see the screen (the baseline grant)
    Control        = 1u << 1,  // session is interactive (vs pure view)
    Clipboard      = 1u << 2,  // bidirectional clipboard sync
    FileTransfer   = 1u << 3,  // file-transfer channel
    Audio          = 1u << 4,  // audio capture/playback
    InputInjection = 1u << 5,  // inject mouse/keyboard on the controlled side
    Elevation      = 1u << 6,  // UAC / Secure Desktop / privileged input
};

inline Capabilities operator|(Capabilities a, Capabilities b) {
    return static_cast<Capabilities>(static_cast<uint32_t>(a) |
                                     static_cast<uint32_t>(b));
}
inline Capabilities operator&(Capabilities a, Capabilities b) {
    return static_cast<Capabilities>(static_cast<uint32_t>(a) &
                                     static_cast<uint32_t>(b));
}
inline Capabilities& operator|=(Capabilities& a, Capabilities b) {
    a = a | b;
    return a;
}

// True iff every bit in `needed` is present in `granted`.
bool hasCapability(Capabilities granted, Capabilities needed);

const char* toString(Capabilities single) noexcept;

// Session lifecycle states.
enum class SessionState {
    Pending,        // created, awaiting consent / handshake
    Authenticating, // Noise handshake + auth (CPace/OPAQUE) in progress
    Active,         // authorized and running
    Denied,         // consent refused or auth failed
    Closed,         // ended (either side disconnected)
};

const char* toString(SessionState state) noexcept;

// Audit event kinds (§3.6.6: audit log keyed by fingerprint).
enum class AuditEvent {
    SessionRequested,
    HandshakeStarted,
    HandshakeCompleted,
    AuthSucceeded,
    AuthFailed,
    CapabilityGranted,
    CapabilityDenied,
    PermissionCheckDenied,  // a runtime action blocked by the gate (default-deny)
    KeyChangeDetected,      // TOFU pin mismatch (§3.6.3)
    SessionActivated,
    SessionClosed,
};

const char* toString(AuditEvent ev) noexcept;

struct AuditRecord {
    uint64_t timestamp_us = 0;
    std::string peer_fingerprint;   // the subject key (§3.6.6)
    AuditEvent event;
    std::string detail;             // free-form context (capability name, reason)
};

// Identity of the remote party as known at session-setup time.
struct PeerInfo {
    std::string redesk_id;          // routing handle (not identity)
    std::string fingerprint;        // BLAKE2b safety number == the identity
    crypto::PublicKey static_key{}; // for pinning / KK
};

// A single remote-control session. Holds the granted capability set, the gate,
// and a back-reference to the manager's audit log. Created via SessionManager.
class Session {
public:
    Session(uint64_t id, PeerInfo peer);

    uint64_t id() const { return id_; }
    const PeerInfo& peer() const { return peer_; }
    SessionState state() const { return state_; }
    Capabilities granted() const { return granted_; }

    // Default-deny gate: returns true only if `needed` was explicitly granted
    // AND the session is Active. A false result is the caller's cue to refuse the
    // action; the manager logs a PermissionCheckDenied audit record.
    bool isAllowed(Capabilities needed) const;

    void setState(SessionState s) { state_ = s; }
    void grant(Capabilities caps) { granted_ |= caps; }

private:
    uint64_t id_;
    PeerInfo peer_;
    SessionState state_ = SessionState::Pending;
    Capabilities granted_ = Capabilities::None; // default-deny: nothing granted
};

// Callback surface so the UI/service observe lifecycle + audit without polling.
using SessionStateCallback =
    std::function<void(uint64_t session_id, SessionState)>;
using AuditCallback = std::function<void(const AuditRecord&)>;

// Orchestrates the session lifecycle and enforces the permission model. The
// service constructs one per machine; multiple concurrent peer sessions are
// supported (each with its own capability grant).
class SessionManager {
public:
    SessionManager();
    ~SessionManager();

    // Begin a session for a peer. Created Pending with NO capabilities (default-
    // deny). Returns the session id. Logs SessionRequested.
    Result<uint64_t> beginSession(const PeerInfo& peer);

    // Record handshake progress (driven by the crypto layer). On completion the
    // manager verifies the peer fingerprint against allow/deny lists + TOFU pin.
    Status onHandshakeStarted(uint64_t session_id);
    Status onHandshakeCompleted(uint64_t session_id,
                                const std::string& peer_fingerprint);

    // Grant the consented capability set and activate (§3.6.6 explicit grant).
    // Rejects capabilities not present in the machine's policy ceiling.
    Status authorize(uint64_t session_id, Capabilities requested);

    // Deny the session (consent refused / auth failed). Logs and closes.
    Status deny(uint64_t session_id, const std::string& reason);

    // Runtime gate the service calls before performing a capability-bound action
    // (e.g. inject input). Default-deny: false unless explicitly granted + Active.
    // A denial is audited.
    bool checkPermission(uint64_t session_id, Capabilities needed);

    Status closeSession(uint64_t session_id);

    // Observers.
    bool isBeingControlled() const;          // any Active session with Control
    const Session* find(uint64_t session_id) const;

    void setStateCallback(SessionStateCallback cb);
    void setAuditCallback(AuditCallback cb);

    // Audit log (keyed by fingerprint, §3.6.6). Full history in memory; the real
    // service persists/rotates this.
    const std::vector<AuditRecord>& auditLog() const;

    // Policy: peer allow/deny lists + the capability ceiling a session may be
    // granted (default-deny: ceiling starts at ViewOnly only).
    void setCapabilityCeiling(Capabilities ceiling);
    void allowPeer(const std::string& fingerprint);
    void denyPeer(const std::string& fingerprint);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace redesk::session
