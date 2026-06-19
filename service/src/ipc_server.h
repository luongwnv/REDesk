#pragma once

// REDesk service — local IPC server (ADR-001 §3.5 "UI", IPC hardening corrections).
//
// The privileged service exposes a *local-only* control channel that the
// unprivileged Qt Quick UI client connects to. This is a trust boundary: the UI
// runs as a normal user and is impersonable, so per ADR §3.5:
//
//   * The endpoint MUST carry an explicit security descriptor / restrictive
//     permissions — grant only intended user SID(s) + Administrators + SYSTEM on
//     Windows; mode 0600 owner-only (or a locked-down group) on the UDS. Deny
//     Everyone / anonymous.
//   * The endpoint MUST NOT live in the per-session object namespace (a low-priv
//     session could otherwise squat the name). Use a global, ACL'd name.
//   * Every connection MUST be authenticated (peer credential check: SO_PEERCRED
//     / LOCAL_PEERCRED / GetNamedPipeClientProcessId + token check) AND every
//     message MUST be validated as untrusted input (length-checked, schema-
//     validated, capability-gated) before it touches the engine.
//
// In the default stub build (REDESK_USE_REAL_BACKENDS=OFF) this is a portable
// Unix-domain-socket listener that compiles on any POSIX host with no external
// deps. The OS-specific ACL hardening + Windows named-pipe path are sketched and
// guarded behind #ifdef so the stub stays clean on macOS/clang. The real
// production transport is QLocalSocket (named pipe / UDS) — see TODO(ADR §3.5).

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "core/common/types.h"

namespace redesk::service {

// A single inbound IPC request, already framed (length-prefixed) and copied out
// of the socket. Treated as fully untrusted until authenticated + validated.
struct IpcMessage {
    std::vector<uint8_t> payload;
};

// Result the handler hands back to be framed and written to the peer.
struct IpcReply {
    std::vector<uint8_t> payload;
};

// Identity of the connected peer, resolved from OS peer credentials. The IPC
// server fills this in before invoking the handler so policy can be enforced.
struct PeerIdentity {
    bool authenticated = false;  // peer credential check passed
    uint32_t uid = 0;            // POSIX uid (LOCAL_PEERCRED/SO_PEERCRED)
    uint32_t pid = 0;            // peer pid (audit / rate-limit keying)
    std::string descriptor;      // human-readable, for the audit log
};

// Handler signature: invoked per validated request on an authenticated peer.
// MUST be cheap / non-blocking-ish; long work is dispatched to the engine.
using IpcMessageHandler =
    std::function<IpcReply(const PeerIdentity&, const IpcMessage&)>;

// Abstract local IPC server. The factory returns the stub UDS implementation by
// default; the real QLocalSocket-backed server is selected when Qt is present.
class IpcServer {
public:
    struct Config {
        // Stub/UDS: filesystem socket path. Real/Windows: pipe name
        // (\\.\pipe\REDesk-service). Kept out of the per-session namespace.
        std::string endpoint;
        // Hard cap on a single framed message; oversized frames are rejected
        // before allocation (DoS guard on the trust boundary).
        size_t max_message_bytes = 1u << 20;  // 1 MiB
    };

    virtual ~IpcServer() = default;

    // Bind + listen + apply endpoint hardening (perms/ACL). Idempotent-safe to
    // call once. Returns PermissionDenied if the endpoint cannot be locked down.
    virtual Status start(const Config& config, IpcMessageHandler handler) = 0;

    // Stop accepting, drain, and remove the endpoint (unlink the UDS path).
    virtual void stop() = 0;

    // True between a successful start() and stop().
    virtual bool running() const = 0;

    // The endpoint actually bound (after any namespacing). For logs/UI handshake.
    virtual std::string endpoint() const = 0;
};

// Factory. Default build returns the portable UDS stub.
std::unique_ptr<IpcServer> CreateIpcServer();

}  // namespace redesk::service
