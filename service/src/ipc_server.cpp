// REDesk service — portable local IPC server stub (ADR-001 §3.5).
//
// Default (REDESK_USE_REAL_BACKENDS=OFF) implementation: a Unix-domain-socket
// listener with length-prefixed framing, a single accept thread, peer-credential
// authentication, and per-message size validation. It compiles with std + POSIX
// only (and degrades to a no-op listener on Windows so the stub tree configures
// everywhere). The real production server is QLocalSocket (named pipe / UDS)
// with the OS ACL hardening below promoted from sketch to enforced.

#include "service/src/ipc_server.h"

#include <cstring>
#include <iostream>

#if !defined(_WIN32)
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <sys/ucred.h>  // LOCAL_PEERCRED / xucred
#endif
#endif

namespace redesk::service {
namespace {

// --- Framing -------------------------------------------------------------
// Wire frame: u32 little-endian length prefix + payload. The length is checked
// against Config::max_message_bytes *before* allocation. This is a hostile
// boundary; never trust the declared length.
constexpr size_t kFrameHeaderBytes = 4;

#if !defined(_WIN32)

// Resolve POSIX peer credentials on an accepted socket. This is the
// authentication step ADR §3.5 mandates: the UI is unprivileged and impersonable
// so we must learn *who* connected before honoring any request.
PeerIdentity AuthenticatePeer(int conn_fd) {
    PeerIdentity id;
#if defined(__APPLE__)
    struct xucred cred {};
    socklen_t len = sizeof(cred);
    if (getsockopt(conn_fd, SOL_LOCAL, LOCAL_PEERCRED, &cred, &len) == 0 &&
        cred.cr_version == XUCRED_VERSION) {
        id.uid = cred.cr_uid;
        id.authenticated = true;
    }
    // macOS also exposes the peer pid via LOCAL_PEERPID for audit keying.
    pid_t peer_pid = 0;
    socklen_t plen = sizeof(peer_pid);
    if (getsockopt(conn_fd, SOL_LOCAL, LOCAL_PEERPID, &peer_pid, &plen) == 0) {
        id.pid = static_cast<uint32_t>(peer_pid);
    }
#elif defined(__linux__)
    struct ucred cred {};
    socklen_t len = sizeof(cred);
    if (getsockopt(conn_fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) == 0) {
        id.uid = cred.uid;
        id.pid = static_cast<uint32_t>(cred.pid);
        id.authenticated = true;
    }
#endif
    id.descriptor = "uid=" + std::to_string(id.uid) +
                    " pid=" + std::to_string(id.pid);
    // TODO(ADR §3.5): beyond uid match, additionally verify the peer's signed
    // binary identity (code signature / trusted install path) and run the
    // in-channel auth (CPace/OPAQUE handshake) before granting any capability.
    return id;
}

bool ReadExact(int fd, uint8_t* buf, size_t n) {
    size_t got = 0;
    while (got < n) {
        ssize_t r = ::read(fd, buf + got, n - got);
        if (r <= 0) return false;
        got += static_cast<size_t>(r);
    }
    return true;
}

bool WriteExact(int fd, const uint8_t* buf, size_t n) {
    size_t put = 0;
    while (put < n) {
        ssize_t w = ::write(fd, buf + put, n - put);
        if (w <= 0) return false;
        put += static_cast<size_t>(w);
    }
    return true;
}

#endif  // !_WIN32

// Portable UDS-backed IPC server. On Windows this compiles to a documented
// no-op so the stub tree configures; the real Windows path is a named pipe with
// an explicit SECURITY_DESCRIPTOR (see #ifdef _WIN32 block).
class UdsIpcServer final : public IpcServer {
public:
    ~UdsIpcServer() override { stop(); }

    Status start(const Config& config, IpcMessageHandler handler) override {
        config_ = config;
        handler_ = std::move(handler);

#if defined(_WIN32)
        // TODO(ADR §3.5): real Windows server.
        //   * CreateNamedPipeW(L"\\\\.\\pipe\\REDesk-service", ...)
        //   * Build a SECURITY_DESCRIPTOR via SDDL granting only the intended
        //     user SID(s) + BUILTIN\Administrators + NT AUTHORITY\SYSTEM, and
        //     explicitly DENYing Everyone (WD) and ANONYMOUS LOGON (AN). Pass it
        //     in SECURITY_ATTRIBUTES so the pipe is ACL'd from creation.
        //   * Authenticate each client with GetNamedPipeClientProcessId +
        //     OpenProcessToken and reject mismatched SIDs. Do NOT
        //     ImpersonateNamedPipeClient and act on the client's behalf.
        //   * Use the global pipe namespace (already global), never a per-session
        //     local namespace a low-priv session could squat.
        std::cerr << "[ipc] Windows named-pipe server not built in stub; "
                     "endpoint=" << config_.endpoint << "\n";
        endpoint_ = config_.endpoint;
        running_ = true;  // pretend-listening so --foreground smoke runs.
        return Status::success();
#else
        endpoint_ = config_.endpoint;
        listen_fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (listen_fd_ < 0) {
            return Status::error(ErrorCode::Internal, "socket() failed");
        }

        // Bind. Unlink any stale endpoint first (a crashed prior instance leaves
        // the path behind). The directory holding it should itself be root-owned
        // 0755 in production so an attacker can't pre-create/symlink the path.
        ::unlink(endpoint_.c_str());
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        if (endpoint_.size() >= sizeof(addr.sun_path)) {
            stop();
            return Status::error(ErrorCode::InvalidArgument,
                                 "UDS path too long: " + endpoint_);
        }
        std::strncpy(addr.sun_path, endpoint_.c_str(),
                     sizeof(addr.sun_path) - 1);

        if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr),
                   sizeof(addr)) != 0) {
            stop();
            return Status::error(ErrorCode::PermissionDenied,
                                 "bind() failed on " + endpoint_);
        }

        // ENDPOINT HARDENING (ADR §3.5): restrict to owner-only. This is the
        // POSIX analogue of the Windows security descriptor. For a multi-user
        // box where a specific desktop user must reach a root service, switch to
        // a dedicated group (0660 + chown root:redesk) instead of 0600.
        if (::chmod(endpoint_.c_str(), S_IRUSR | S_IWUSR) != 0) {
            stop();
            return Status::error(ErrorCode::PermissionDenied,
                                 "chmod 0600 failed on " + endpoint_);
        }

        if (::listen(listen_fd_, /*backlog=*/4) != 0) {
            stop();
            return Status::error(ErrorCode::Internal, "listen() failed");
        }

        running_ = true;
        accept_thread_ = std::thread(&UdsIpcServer::AcceptLoop, this);
        std::cerr << "[ipc] listening on " << endpoint_ << " (uds, 0600)\n";
        return Status::success();
#endif
    }

    void stop() override {
        running_ = false;
#if !defined(_WIN32)
        if (listen_fd_ >= 0) {
            ::shutdown(listen_fd_, SHUT_RDWR);
            ::close(listen_fd_);
            listen_fd_ = -1;
        }
        if (accept_thread_.joinable()) {
            accept_thread_.join();
        }
        if (!endpoint_.empty()) {
            ::unlink(endpoint_.c_str());
        }
#endif
    }

    bool running() const override { return running_; }
    std::string endpoint() const override { return endpoint_; }

private:
#if !defined(_WIN32)
    void AcceptLoop() {
        while (running_) {
            int conn = ::accept(listen_fd_, nullptr, nullptr);
            if (conn < 0) {
                if (!running_) break;       // stop() shut us down
                continue;                   // transient; keep serving
            }
            ServeConnection(conn);
            ::close(conn);
        }
    }

    // One synchronous request/response per connection in the stub (the real
    // server multiplexes the IPC protocol from proto/). Authenticates the peer,
    // size-validates each frame, then dispatches to the handler.
    void ServeConnection(int conn) {
        PeerIdentity peer = AuthenticatePeer(conn);
        if (!peer.authenticated) {
            std::cerr << "[ipc] rejecting unauthenticated peer\n";
            return;  // ADR §3.5: deny anything we cannot attribute.
        }

        uint8_t header[kFrameHeaderBytes];
        if (!ReadExact(conn, header, kFrameHeaderBytes)) return;
        const uint32_t len = static_cast<uint32_t>(header[0]) |
                             (static_cast<uint32_t>(header[1]) << 8) |
                             (static_cast<uint32_t>(header[2]) << 16) |
                             (static_cast<uint32_t>(header[3]) << 24);

        // Untrusted length: reject before allocating (DoS guard).
        if (len == 0 || len > config_.max_message_bytes) {
            std::cerr << "[ipc] frame rejected: len=" << len
                      << " cap=" << config_.max_message_bytes << "\n";
            return;
        }

        IpcMessage msg;
        msg.payload.resize(len);
        if (!ReadExact(conn, msg.payload.data(), len)) return;

        IpcReply reply = handler_ ? handler_(peer, msg) : IpcReply{};

        uint8_t out_hdr[kFrameHeaderBytes];
        const uint32_t out_len = static_cast<uint32_t>(reply.payload.size());
        out_hdr[0] = static_cast<uint8_t>(out_len & 0xFF);
        out_hdr[1] = static_cast<uint8_t>((out_len >> 8) & 0xFF);
        out_hdr[2] = static_cast<uint8_t>((out_len >> 16) & 0xFF);
        out_hdr[3] = static_cast<uint8_t>((out_len >> 24) & 0xFF);
        if (!WriteExact(conn, out_hdr, kFrameHeaderBytes)) return;
        if (out_len) WriteExact(conn, reply.payload.data(), out_len);
    }

    int listen_fd_ = -1;
    std::thread accept_thread_;
#endif  // !_WIN32

    Config config_;
    IpcMessageHandler handler_;
    std::string endpoint_;
    std::atomic<bool> running_{false};
};

}  // namespace

std::unique_ptr<IpcServer> CreateIpcServer() {
    // TODO(ADR §3.5): when REDESK_USE_REAL_BACKENDS=ON and Qt is present, return
    // a QLocalSocket-backed server instead (named pipe on Windows / UDS
    // elsewhere) sharing the proto/ IPC schema with the UI client.
    return std::make_unique<UdsIpcServer>();
}

}  // namespace redesk::service
