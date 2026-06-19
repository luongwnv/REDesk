#pragma once

// Portable BSD-socket wrapper for the REDesk servers (ADR-001 §3.3).
//
// Both the rendezvous (control listener) and the relay (UDP forwarder) need a
// minimal, dependency-free socket layer that compiles on macOS/Linux/Windows.
// This is deliberately tiny: enough to stand up the listener skeletons in the
// stub build without pulling in libjuice / asio / Qt. The real transport on the
// *client* side is ICE-managed (libjuice) per §3.3; the servers themselves only
// need plain UDP/TCP sockets, so a hand-rolled wrapper is appropriate here.
//
// Platform differences (winsock vs POSIX) are guarded inline. On Windows the
// caller must construct one NetInit at process start to pump WSAStartup.

#include <cstdint>
#include <string>
#include <vector>

#include "core/common/types.h"

namespace redesk::server::net {

// Underlying OS socket handle. SOCKET on Windows is an unsigned pointer-width
// handle; an int fd on POSIX. We normalize to int64_t and treat <0 as invalid
// so call sites are platform-agnostic.
using SocketHandle = std::int64_t;
inline constexpr SocketHandle kInvalidSocket = -1;

// A resolved peer address (v4/v6) kept opaque to callers. `display` is a
// host:port string purely for logging / registry keys.
struct Endpoint {
    std::string host;      // numeric IP (as presented by the OS), e.g. "203.0.113.7"
    std::uint16_t port = 0;
    bool ipv6 = false;

    std::string display() const {
        if (ipv6) return "[" + host + "]:" + std::to_string(port);
        return host + ":" + std::to_string(port);
    }
    bool operator==(const Endpoint& o) const {
        return port == o.port && ipv6 == o.ipv6 && host == o.host;
    }
};

// Process-wide network init (Winsock pump on Windows; no-op on POSIX). RAII so
// servers can `net::NetInit guard;` at the top of main().
class NetInit {
public:
    NetInit();
    ~NetInit();
    NetInit(const NetInit&) = delete;
    NetInit& operator=(const NetInit&) = delete;
};

// Thin owning wrapper around one datagram or stream socket. Move-only.
class Socket {
public:
    Socket() = default;
    explicit Socket(SocketHandle h) : handle_(h) {}
    ~Socket();

    Socket(Socket&& o) noexcept : handle_(o.handle_) { o.handle_ = kInvalidSocket; }
    Socket& operator=(Socket&& o) noexcept;
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    bool valid() const { return handle_ != kInvalidSocket; }
    SocketHandle handle() const { return handle_; }
    void close();

    // Bind a UDP socket to the given port (all interfaces). On success the
    // socket is ready for recv_from/send_to.
    static Result<Socket> bind_udp(std::uint16_t port);

    // Bind+listen a TCP socket on the given port.
    static Result<Socket> listen_tcp(std::uint16_t port, int backlog = 64);

    // Blocking accept on a listening TCP socket. Fills `peer`.
    Result<Socket> accept(Endpoint& peer);

    // Datagram recv. Fills `from`. Returns bytes read (>=0) or a Status error;
    // ErrorCode::Again on a non-fatal would-block / interrupted read.
    Result<std::size_t> recv_from(std::uint8_t* buf, std::size_t len, Endpoint& from);

    // Datagram send to a resolved endpoint.
    Status send_to(const std::uint8_t* buf, std::size_t len, const Endpoint& to);

private:
    SocketHandle handle_ = kInvalidSocket;
};

} // namespace redesk::server::net
