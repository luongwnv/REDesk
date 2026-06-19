// SessionManager: lifecycle + default-deny permission gate + fingerprint-keyed
// audit log (ADR-001 §3.6.6). This is real orchestration logic (not a stub) — it
// is platform-agnostic and the same code runs in production; only the backends it
// coordinates (capture/codec/transport/crypto/input) are swapped via factories.

#include "core/session/session.h"

#include <chrono>
#include <map>
#include <set>
#include <unordered_map>

#include "core/common/logging.h"

namespace redesk::session {

bool hasCapability(Capabilities granted, Capabilities needed) {
    return (static_cast<uint32_t>(granted) & static_cast<uint32_t>(needed)) ==
           static_cast<uint32_t>(needed);
}

const char* toString(Capabilities single) noexcept {
    switch (single) {
        case Capabilities::None:           return "None";
        case Capabilities::ViewOnly:       return "ViewOnly";
        case Capabilities::Control:        return "Control";
        case Capabilities::Clipboard:      return "Clipboard";
        case Capabilities::FileTransfer:   return "FileTransfer";
        case Capabilities::Audio:          return "Audio";
        case Capabilities::InputInjection: return "InputInjection";
        case Capabilities::Elevation:      return "Elevation";
    }
    return "Mixed";
}

const char* toString(SessionState state) noexcept {
    switch (state) {
        case SessionState::Pending:        return "Pending";
        case SessionState::Authenticating: return "Authenticating";
        case SessionState::Active:         return "Active";
        case SessionState::Denied:         return "Denied";
        case SessionState::Closed:         return "Closed";
    }
    return "?";
}

const char* toString(AuditEvent ev) noexcept {
    switch (ev) {
        case AuditEvent::SessionRequested:      return "SessionRequested";
        case AuditEvent::HandshakeStarted:      return "HandshakeStarted";
        case AuditEvent::HandshakeCompleted:    return "HandshakeCompleted";
        case AuditEvent::AuthSucceeded:         return "AuthSucceeded";
        case AuditEvent::AuthFailed:            return "AuthFailed";
        case AuditEvent::CapabilityGranted:     return "CapabilityGranted";
        case AuditEvent::CapabilityDenied:      return "CapabilityDenied";
        case AuditEvent::PermissionCheckDenied: return "PermissionCheckDenied";
        case AuditEvent::KeyChangeDetected:     return "KeyChangeDetected";
        case AuditEvent::SessionActivated:      return "SessionActivated";
        case AuditEvent::SessionClosed:         return "SessionClosed";
    }
    return "?";
}

// ---------------------------------------------------------------------------
Session::Session(uint64_t id, PeerInfo peer)
    : id_(id), peer_(std::move(peer)) {}

bool Session::isAllowed(Capabilities needed) const {
    return state_ == SessionState::Active && hasCapability(granted_, needed);
}

// ---------------------------------------------------------------------------
namespace {
uint64_t nowMicros() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<microseconds>(system_clock::now().time_since_epoch())
            .count());
}
} // namespace

struct SessionManager::Impl {
    std::map<uint64_t, Session> sessions;
    uint64_t next_id = 1;

    std::vector<AuditRecord> audit;
    SessionStateCallback state_cb;
    AuditCallback audit_cb;

    // Policy. Default-deny: ceiling is ViewOnly only until the operator raises it.
    Capabilities ceiling = Capabilities::ViewOnly;
    std::set<std::string> allow_list;  // explicit allow (pinned fleets)
    std::set<std::string> deny_list;   // hard block
    // TOFU pin: fingerprint first seen per redesk_id (§3.6.3 key-change alert).
    std::unordered_map<std::string, std::string> tofu_pins; // id -> fingerprint

    void record(const std::string& fingerprint, AuditEvent ev,
                std::string detail) {
        AuditRecord r;
        r.timestamp_us = nowMicros();
        r.peer_fingerprint = fingerprint;
        r.event = ev;
        r.detail = std::move(detail);
        REDESK_LOG(Info, "session")
            << toString(ev) << " peer=" << fingerprint << " " << r.detail;
        audit.push_back(r);
        if (audit_cb) {
            audit_cb(r);
        }
    }

    void transition(Session& s, SessionState to) {
        s.setState(to);
        if (state_cb) {
            state_cb(s.id(), to);
        }
    }
};

SessionManager::SessionManager() : impl_(std::make_unique<Impl>()) {}
SessionManager::~SessionManager() = default;

Result<uint64_t> SessionManager::beginSession(const PeerInfo& peer) {
    if (!impl_->deny_list.empty() &&
        impl_->deny_list.count(peer.fingerprint)) {
        impl_->record(peer.fingerprint, AuditEvent::CapabilityDenied,
                      "peer on deny-list");
        return Result<uint64_t>::fail(ErrorCode::PermissionDenied,
                                      "peer is on the deny list");
    }
    const uint64_t id = impl_->next_id++;
    impl_->sessions.emplace(id, Session(id, peer));
    impl_->record(peer.fingerprint, AuditEvent::SessionRequested,
                  "id=" + peer.redesk_id);
    return Result<uint64_t>::good(id);
}

Status SessionManager::onHandshakeStarted(uint64_t session_id) {
    auto it = impl_->sessions.find(session_id);
    if (it == impl_->sessions.end()) {
        return Status::error(ErrorCode::NotFound, "unknown session");
    }
    impl_->transition(it->second, SessionState::Authenticating);
    impl_->record(it->second.peer().fingerprint, AuditEvent::HandshakeStarted,
                  "");
    return Status::success();
}

Status SessionManager::onHandshakeCompleted(uint64_t session_id,
                                            const std::string& peer_fingerprint) {
    auto it = impl_->sessions.find(session_id);
    if (it == impl_->sessions.end()) {
        return Status::error(ErrorCode::NotFound, "unknown session");
    }
    Session& s = it->second;

    // The fingerprint learned from the handshake is the authoritative identity;
    // record it on the session (it may have been empty on a first-contact XK).
    PeerInfo peer = s.peer();
    if (peer.fingerprint.empty()) {
        peer.fingerprint = peer_fingerprint;
    }
    impl_->record(peer.fingerprint, AuditEvent::HandshakeCompleted, "");

    // Deny-list re-check against the now-verified fingerprint.
    if (impl_->deny_list.count(peer_fingerprint)) {
        impl_->record(peer_fingerprint, AuditEvent::AuthFailed,
                      "peer on deny-list (post-handshake)");
        impl_->transition(s, SessionState::Denied);
        return Status::error(ErrorCode::PermissionDenied, "peer denied");
    }

    // TOFU pin check (§3.6.3): alert on a changed key for a known routing id.
    auto pin = impl_->tofu_pins.find(peer.redesk_id);
    if (pin != impl_->tofu_pins.end() && pin->second != peer_fingerprint) {
        impl_->record(peer_fingerprint, AuditEvent::KeyChangeDetected,
                      "id=" + peer.redesk_id + " expected=" + pin->second);
        // Not auto-denied: surfaced for the out-of-band-compare UX to resolve.
        // The operator must explicitly re-authorize via authorize().
    } else if (pin == impl_->tofu_pins.end()) {
        impl_->tofu_pins[peer.redesk_id] = peer_fingerprint; // first contact
    }

    return Status::success();
}

Status SessionManager::authorize(uint64_t session_id, Capabilities requested) {
    auto it = impl_->sessions.find(session_id);
    if (it == impl_->sessions.end()) {
        return Status::error(ErrorCode::NotFound, "unknown session");
    }
    Session& s = it->second;
    const std::string& fp = s.peer().fingerprint;

    // Clamp the request to the machine's capability ceiling (least-privilege).
    // ViewOnly is always implicitly part of any grant.
    const auto requested_bits = static_cast<uint32_t>(requested);
    const auto ceiling_bits = static_cast<uint32_t>(impl_->ceiling) |
                              static_cast<uint32_t>(Capabilities::ViewOnly);
    const auto allowed_bits = requested_bits & ceiling_bits;
    const auto refused_bits = requested_bits & ~ceiling_bits;

    if (refused_bits != 0) {
        impl_->record(fp, AuditEvent::CapabilityDenied,
                      "above ceiling: requested=" +
                          std::to_string(requested_bits) +
                          " ceiling=" + std::to_string(ceiling_bits));
    }
    if (allowed_bits == 0) {
        impl_->transition(s, SessionState::Denied);
        return Status::error(ErrorCode::PermissionDenied,
                             "no requested capability is within policy");
    }

    s.grant(static_cast<Capabilities>(allowed_bits | static_cast<uint32_t>(
                                          Capabilities::ViewOnly)));
    impl_->record(fp, AuditEvent::CapabilityGranted,
                  "granted=" + std::to_string(allowed_bits |
                                              static_cast<uint32_t>(
                                                  Capabilities::ViewOnly)));
    impl_->record(fp, AuditEvent::AuthSucceeded, "");
    impl_->transition(s, SessionState::Active);
    impl_->record(fp, AuditEvent::SessionActivated, "");
    return Status::success();
}

Status SessionManager::deny(uint64_t session_id, const std::string& reason) {
    auto it = impl_->sessions.find(session_id);
    if (it == impl_->sessions.end()) {
        return Status::error(ErrorCode::NotFound, "unknown session");
    }
    Session& s = it->second;
    impl_->record(s.peer().fingerprint, AuditEvent::AuthFailed, reason);
    impl_->transition(s, SessionState::Denied);
    return Status::success();
}

bool SessionManager::checkPermission(uint64_t session_id, Capabilities needed) {
    auto it = impl_->sessions.find(session_id);
    if (it == impl_->sessions.end()) {
        return false; // default-deny: unknown session
    }
    const Session& s = it->second;
    if (s.isAllowed(needed)) {
        return true;
    }
    impl_->record(s.peer().fingerprint, AuditEvent::PermissionCheckDenied,
                  std::string("needed=") +
                      std::to_string(static_cast<uint32_t>(needed)) +
                      " state=" + toString(s.state()));
    return false;
}

Status SessionManager::closeSession(uint64_t session_id) {
    auto it = impl_->sessions.find(session_id);
    if (it == impl_->sessions.end()) {
        return Status::error(ErrorCode::NotFound, "unknown session");
    }
    Session& s = it->second;
    impl_->transition(s, SessionState::Closed);
    impl_->record(s.peer().fingerprint, AuditEvent::SessionClosed, "");
    return Status::success();
}

bool SessionManager::isBeingControlled() const {
    for (const auto& [id, s] : impl_->sessions) {
        if (s.state() == SessionState::Active &&
            hasCapability(s.granted(), Capabilities::Control)) {
            return true;
        }
    }
    return false;
}

const Session* SessionManager::find(uint64_t session_id) const {
    auto it = impl_->sessions.find(session_id);
    return it == impl_->sessions.end() ? nullptr : &it->second;
}

void SessionManager::setStateCallback(SessionStateCallback cb) {
    impl_->state_cb = std::move(cb);
}
void SessionManager::setAuditCallback(AuditCallback cb) {
    impl_->audit_cb = std::move(cb);
}

const std::vector<AuditRecord>& SessionManager::auditLog() const {
    return impl_->audit;
}

void SessionManager::setCapabilityCeiling(Capabilities ceiling) {
    impl_->ceiling = ceiling;
}
void SessionManager::allowPeer(const std::string& fingerprint) {
    impl_->allow_list.insert(fingerprint);
    impl_->deny_list.erase(fingerprint);
}
void SessionManager::denyPeer(const std::string& fingerprint) {
    impl_->deny_list.insert(fingerprint);
    impl_->allow_list.erase(fingerprint);
}

} // namespace redesk::session
