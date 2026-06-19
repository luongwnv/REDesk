// Portable BSD-socket wrapper implementation (ADR-001 §3.3).
//
// Stub-build-clean: standard library + OS sockets only. No libjuice here — the
// servers use plain UDP/TCP; ICE/NAT traversal lives on the client transport.

#include "server/common/net.h"

#include <cstring>

#if defined(_WIN32)
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "Ws2_32.lib")
using socklen_t = int;
#else
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <sys/types.h>
#  include <unistd.h>
#  include <cerrno>
#endif

namespace redesk::server::net {

namespace {

#if defined(_WIN32)
void close_handle(SocketHandle h) {
    if (h != kInvalidSocket) ::closesocket(static_cast<SOCKET>(h));
}
int last_error() { return ::WSAGetLastError(); }
bool would_block(int e) { return e == WSAEWOULDBLOCK || e == WSAEINTR; }
#else
void close_handle(SocketHandle h) {
    if (h != kInvalidSocket) ::close(static_cast<int>(h));
}
int last_error() { return errno; }
bool would_block(int e) { return e == EAGAIN || e == EWOULDBLOCK || e == EINTR; }
#endif

// Fill an Endpoint from a sockaddr_storage written by recv/accept.
Endpoint endpoint_from_sockaddr(const sockaddr_storage& ss) {
    Endpoint ep;
    char buf[INET6_ADDRSTRLEN] = {0};
    if (ss.ss_family == AF_INET6) {
        const auto* a = reinterpret_cast<const sockaddr_in6*>(&ss);
        ::inet_ntop(AF_INET6, &a->sin6_addr, buf, sizeof(buf));
        ep.host = buf;
        ep.port = ntohs(a->sin6_port);
        ep.ipv6 = true;
    } else {
        const auto* a = reinterpret_cast<const sockaddr_in*>(&ss);
        ::inet_ntop(AF_INET, &a->sin_addr, buf, sizeof(buf));
        ep.host = buf;
        ep.port = ntohs(a->sin_port);
        ep.ipv6 = false;
    }
    return ep;
}

// Resolve an Endpoint back into a sockaddr for send_to. Numeric-only (no DNS):
// the registry stores numeric IPs the OS already presented to us.
bool sockaddr_from_endpoint(const Endpoint& ep, sockaddr_storage& ss, socklen_t& len) {
    std::memset(&ss, 0, sizeof(ss));
    if (ep.ipv6) {
        auto* a = reinterpret_cast<sockaddr_in6*>(&ss);
        a->sin6_family = AF_INET6;
        a->sin6_port = htons(ep.port);
        if (::inet_pton(AF_INET6, ep.host.c_str(), &a->sin6_addr) != 1) return false;
        len = sizeof(sockaddr_in6);
    } else {
        auto* a = reinterpret_cast<sockaddr_in*>(&ss);
        a->sin_family = AF_INET;
        a->sin_port = htons(ep.port);
        if (::inet_pton(AF_INET, ep.host.c_str(), &a->sin_addr) != 1) return false;
        len = sizeof(sockaddr_in);
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// NetInit
// ---------------------------------------------------------------------------
NetInit::NetInit() {
#if defined(_WIN32)
    WSADATA wsa;
    ::WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
}
NetInit::~NetInit() {
#if defined(_WIN32)
    ::WSACleanup();
#endif
}

// ---------------------------------------------------------------------------
// Socket
// ---------------------------------------------------------------------------
Socket::~Socket() { close(); }

Socket& Socket::operator=(Socket&& o) noexcept {
    if (this != &o) {
        close();
        handle_ = o.handle_;
        o.handle_ = kInvalidSocket;
    }
    return *this;
}

void Socket::close() {
    close_handle(handle_);
    handle_ = kInvalidSocket;
}

Result<Socket> Socket::bind_udp(std::uint16_t port) {
    SocketHandle s = static_cast<SocketHandle>(::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
    if (s == kInvalidSocket) {
        return Result<Socket>::fail(ErrorCode::Internal, "udp socket() failed");
    }
    int yes = 1;
    ::setsockopt(static_cast<int>(s), SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char*>(&yes), sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (::bind(static_cast<int>(s), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        close_handle(s);
        return Result<Socket>::fail(ErrorCode::Internal,
                                    "udp bind() failed on port " + std::to_string(port));
    }
    return Result<Socket>::good(Socket(s));
}

Result<Socket> Socket::listen_tcp(std::uint16_t port, int backlog) {
    SocketHandle s = static_cast<SocketHandle>(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (s == kInvalidSocket) {
        return Result<Socket>::fail(ErrorCode::Internal, "tcp socket() failed");
    }
    int yes = 1;
    ::setsockopt(static_cast<int>(s), SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char*>(&yes), sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (::bind(static_cast<int>(s), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        close_handle(s);
        return Result<Socket>::fail(ErrorCode::Internal,
                                    "tcp bind() failed on port " + std::to_string(port));
    }
    if (::listen(static_cast<int>(s), backlog) != 0) {
        close_handle(s);
        return Result<Socket>::fail(ErrorCode::Internal, "listen() failed");
    }
    return Result<Socket>::good(Socket(s));
}

Result<Socket> Socket::accept(Endpoint& peer) {
    sockaddr_storage ss{};
    socklen_t len = sizeof(ss);
    SocketHandle c = static_cast<SocketHandle>(
        ::accept(static_cast<int>(handle_), reinterpret_cast<sockaddr*>(&ss), &len));
    if (c == kInvalidSocket) {
        int e = last_error();
        return Result<Socket>::fail(would_block(e) ? ErrorCode::Again : ErrorCode::ConnectionLost,
                                    "accept() failed");
    }
    peer = endpoint_from_sockaddr(ss);
    return Result<Socket>::good(Socket(c));
}

Result<std::size_t> Socket::recv_from(std::uint8_t* buf, std::size_t len, Endpoint& from) {
    sockaddr_storage ss{};
    socklen_t slen = sizeof(ss);
    auto n = ::recvfrom(static_cast<int>(handle_), reinterpret_cast<char*>(buf),
                        static_cast<int>(len), 0, reinterpret_cast<sockaddr*>(&ss), &slen);
    if (n < 0) {
        int e = last_error();
        return Result<std::size_t>::fail(would_block(e) ? ErrorCode::Again : ErrorCode::ConnectionLost,
                                         "recvfrom() failed");
    }
    from = endpoint_from_sockaddr(ss);
    return Result<std::size_t>::good(static_cast<std::size_t>(n));
}

Status Socket::send_to(const std::uint8_t* buf, std::size_t len, const Endpoint& to) {
    sockaddr_storage ss{};
    socklen_t slen = 0;
    if (!sockaddr_from_endpoint(to, ss, slen)) {
        return Status::error(ErrorCode::InvalidArgument, "unparseable endpoint: " + to.display());
    }
    auto n = ::sendto(static_cast<int>(handle_), reinterpret_cast<const char*>(buf),
                      static_cast<int>(len), 0, reinterpret_cast<const sockaddr*>(&ss), slen);
    if (n < 0) {
        int e = last_error();
        return Status::error(would_block(e) ? ErrorCode::Again : ErrorCode::ConnectionLost,
                             "sendto() failed");
    }
    return Status::success();
}

} // namespace redesk::server::net
