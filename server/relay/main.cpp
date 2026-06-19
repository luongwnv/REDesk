// redesk-relay — thin blind-forward relay entry point (ADR-001 §3.3, §3.6.2).
//
// Role (== RustDesk hbbr): the punch-fail fallback. When ICE hole-punching
// fails (CGNAT / symmetric NAT / enterprise — budget 15-30%+ of sessions,
// §3.3), the two peers route their Noise-encrypted UDP through this relay,
// which forwards opaque ciphertext between them by session token and NEVER
// decrypts (zero knowledge of media keys, §3.6.2).
//
// Real deployment is MULTI-REGION / ANYCAST with health checks + graceful
// reselection (§3.3, Risk #6); coturn/eturnal/STUNner are used ONLY for the
// standards-TURN web-client / libdatachannel path (§3.3) — this proprietary
// relay is the default media fallback and is intentionally dumber and cheaper
// than standards TURN.
//
// WIRE (skeleton): each relayed datagram is [u8 token_len][token][ciphertext].
// The token is the allocation handle negotiated out-of-band via the rendezvous
// (or an ALLOCATE request to the relay). The relay strips the token, learns the
// sender's source endpoint on first contact (address learning, symmetric to ICE
// discovery), pairs the two legs, and forwards the ciphertext VERBATIM to the
// peer leg — re-prefixing the peer's own token is unnecessary since the relay
// forwards the raw inner payload. In the real build the framing is the proto/
// transport header (one source of truth) and tokens are 128-bit unguessable
// values. TODO(ADR §3.3): proto/ framing + ALLOCATE control verb + relay
// selection handshake.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "core/common/types.h"
#include "redesk_version.h"
#include "server/common/net.h"
#include "server/relay/allocation_table.h"

namespace {

using namespace redesk;
using namespace redesk::server;

constexpr std::uint16_t kDefaultRelayPort = 21117; // nods to RustDesk hbbr range
constexpr std::int64_t kSessionTtlSeconds = 30;    // idle relayed session TTL
constexpr std::int64_t kExpirySweepSeconds = 10;

std::int64_t now_unix_s() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// Parse [u8 token_len][token][payload...]. Returns false on malformed input so
// the forwarder drops hostile/short datagrams without touching state.
bool parse_datagram(const std::uint8_t* data, std::size_t len, relay::SessionToken& token,
                    const std::uint8_t** payload, std::size_t* payload_len) {
    if (len < 1) return false;
    const std::uint8_t tlen = data[0];
    if (tlen == 0 || static_cast<std::size_t>(1 + tlen) > len) return false;
    token.assign(reinterpret_cast<const char*>(data + 1), tlen);
    *payload = data + 1 + tlen;
    *payload_len = len - 1 - tlen;
    return true;
}

int run(std::uint16_t relay_port) {
    std::printf("redesk-relay %s — thin blind-forward fallback (ADR §3.3/§3.6.2)\n",
                redesk::kVersion);
    std::printf("[relay] this relay NEVER decrypts; it forwards opaque Noise ciphertext only\n");

    net::NetInit net_guard;

    auto table = relay::make_in_memory_allocation_table();

    auto udp = net::Socket::bind_udp(relay_port);
    if (!udp.ok()) {
        std::fprintf(stderr, "[relay] fatal: %s\n", udp.status.message.c_str());
        return 1;
    }
    std::printf("[relay] forwarding UDP/%u\n", relay_port);

    std::int64_t last_sweep = now_unix_s();
    std::vector<std::uint8_t> buf(64 * 1024);

    // Single-threaded forward loop. Stub-only: production uses a readiness-
    // multiplexed event loop (epoll/kqueue/IOCP) + per-region anycast nodes and
    // sheds load via the allocation table TTL. TODO(ADR §3.3): event loop,
    // ALLOCATE control verb, per-token rate-limit, metrics, graceful shutdown.
    for (;;) {
        net::Endpoint from;
        auto r = udp.value.recv_from(buf.data(), buf.size(), from);
        if (r.ok()) {
            relay::SessionToken token;
            const std::uint8_t* payload = nullptr;
            std::size_t payload_len = 0;
            if (parse_datagram(buf.data(), r.value, token, &payload, &payload_len)) {
                const std::int64_t now = now_unix_s();
                // Auto-allocate on first sight of a token. In the real build the
                // allocation is created by an authenticated ALLOCATE request from
                // the rendezvous so the relay isn't an open forwarder; here the
                // session-leg cap in the table (max two peers per token) plus the
                // TTL is the only abuse guard.
                table->allocate(token, now);
                auto dst = table->route(token, from, payload_len, now);
                if (dst.ok()) {
                    // Forward the inner ciphertext VERBATIM to the peer leg,
                    // re-wrapped with the same token so the peer's parser is
                    // symmetric. The relay does not inspect `payload`.
                    std::vector<std::uint8_t> out;
                    out.reserve(1 + token.size() + payload_len);
                    out.push_back(static_cast<std::uint8_t>(token.size()));
                    out.insert(out.end(), token.begin(), token.end());
                    out.insert(out.end(), payload, payload + payload_len);
                    const Status s = udp.value.send_to(out.data(), out.size(), dst.value);
                    if (!s.ok()) {
                        std::fprintf(stderr, "[relay] forward -> %s failed: %s\n",
                                     dst.value.display().c_str(), s.message.c_str());
                    }
                } else if (dst.status.code != ErrorCode::Again) {
                    // Again == peer leg not bound yet (normal during setup); any
                    // other error is a real drop (e.g. token squatting).
                    std::fprintf(stderr, "[relay] drop token=%s from=%s: %s\n", token.c_str(),
                                 from.display().c_str(), dst.status.message.c_str());
                }
            }
        } else if (r.status.code != ErrorCode::Again) {
            std::fprintf(stderr, "[relay] recv error: %s\n", r.status.message.c_str());
        }

        const std::int64_t now = now_unix_s();
        if (now - last_sweep >= kExpirySweepSeconds) {
            const std::size_t removed = table->expire(now, kSessionTtlSeconds);
            if (removed) std::printf("[relay] expired %zu idle sessions (live=%zu)\n", removed,
                                     table->size());
            last_sweep = now;
        }
    }
    return 0;
}

std::uint16_t parse_port(int argc, char** argv) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--port") == 0) {
            return static_cast<std::uint16_t>(std::atoi(argv[i + 1]));
        }
    }
    return kDefaultRelayPort;
}

} // namespace

int main(int argc, char** argv) {
    return run(parse_port(argc, argv));
}
