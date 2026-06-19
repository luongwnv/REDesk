// redesk-rendezvous — signaling server entry point (ADR-001 §2, §3.3, §3.6).
//
// Role (== RustDesk hbbs, but signed-introducer + zero-knowledge):
//   * ID -> endpoint registry (server/rendezvous/registry_store.h)
//   * Ed25519-signs each peer's X25519 static pubkey (server/rendezvous/signer.h)
//   * Brokers ICE candidates / SDP between two peers by a short-lived session
//     token (the BROKER_CANDIDATES family in protocol.h)
//   * ZERO knowledge of media keys (§3.6.2) — only routes opaque blobs + signs
//     public material.
//
// This is a SKELETON: a single-threaded poll loop over a UDP control socket
// (REGISTER / Keepalive / LOOKUP / BROKER_CANDIDATES) plus a TCP listener stub
// for the reliable / TLS-443 path (§3.3 keeps TCP for UDP-blocked networks).
// It compiles and runs in the stub build with no external deps. Real
// production wiring (epoll/io_uring/IOCP event loop, sharded registry,
// rate-limiting, metrics, anycast health) is layered behind
// REDESK_USE_REAL_BACKENDS / TODO markers.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>

#include "core/common/types.h"
#include "redesk_version.h"
#include "server/common/net.h"
#include "server/rendezvous/protocol.h"
#include "server/rendezvous/registry_store.h"
#include "server/rendezvous/signer.h"

namespace {

using namespace redesk;
using namespace redesk::server;

constexpr std::uint16_t kDefaultControlPort = 21116; // nods to RustDesk hbbs range
constexpr std::int64_t kRegistryTtlSeconds = 60;     // NAT binding TTL
constexpr std::int64_t kExpirySweepSeconds = 15;

std::int64_t now_unix_s() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// A pending candidate-broker session: two peers exchanging opaque ICE/SDP blobs
// keyed by a session token. The server only routes; the punch + Noise handshake
// are peer-to-peer (§3.3). If both peers report punch failure the server hands
// back the lowest-RTT relay (out of scope for the skeleton — TODO below).
struct BrokerSession {
    rendezvous::RedeskId caller;          // controller
    rendezvous::RedeskId callee;          // host
    net::Endpoint caller_ep;
    net::Endpoint callee_ep;
    std::int64_t created_unix_s = 0;
};

// Encode a REGISTER_OK / LOOKUP_OK body: [u8 pk_len][pk][u8 sig_len][sig]
// [u8 vk_len][vk]. Opaque to the wire codec; the client parses it symmetrically.
std::vector<std::uint8_t> encode_attestation(const rendezvous::StaticPubKey& pk,
                                             const std::vector<std::uint8_t>& sig,
                                             const std::vector<std::uint8_t>& vk) {
    std::vector<std::uint8_t> b;
    auto push_lv = [&](const std::vector<std::uint8_t>& v) {
        b.push_back(static_cast<std::uint8_t>(v.size() > 255 ? 255 : v.size()));
        b.insert(b.end(), v.begin(), v.begin() + (v.size() > 255 ? 255 : v.size()));
    };
    push_lv(pk);
    push_lv(sig);
    push_lv(vk);
    return b;
}

void send_frame(net::Socket& sock, const net::Endpoint& to, rendezvous::MessageType type,
                const rendezvous::RedeskId& id, std::vector<std::uint8_t> body) {
    rendezvous::Frame f;
    f.type = type;
    f.id = id;
    f.body = std::move(body);
    const auto bytes = rendezvous::encode_frame(f);
    const Status s = sock.send_to(bytes.data(), bytes.size(), to);
    if (!s.ok()) {
        std::fprintf(stderr, "[rendezvous] send %s -> %s failed: %s\n",
                     std::to_string(static_cast<int>(type)).c_str(),
                     to.display().c_str(), s.message.c_str());
    }
}

void handle_register(net::Socket& sock, const net::Endpoint& from, const rendezvous::Frame& f,
                     rendezvous::RegistryStore& reg, rendezvous::Signer& signer) {
    // REGISTER body == the peer's 32B X25519 static pubkey (+ optional caps; the
    // skeleton reads only the key). Endpoint is the OBSERVED packet source, not
    // anything in the body — anti-spoof per protocol.h.
    if (f.body.size() < rendezvous::kStaticPubKeyLen) {
        std::fprintf(stderr, "[rendezvous] REGISTER from %s: short pubkey\n", from.display().c_str());
        return;
    }
    rendezvous::PeerRecord rec;
    rec.id = f.id;
    rec.endpoint = from;
    rec.static_pubkey.assign(f.body.begin(), f.body.begin() + rendezvous::kStaticPubKeyLen);
    rec.last_seen_unix_s = now_unix_s();

    const Status s = reg.upsert(rec);
    if (!s.ok()) {
        std::fprintf(stderr, "[rendezvous] REGISTER %s rejected: %s\n", f.id.c_str(), s.message.c_str());
        return;
    }

    // Hand the peer a server-attested copy of its own key so it can forward a
    // signed introduction (§3.6.3).
    auto sig = signer.sign_peer_key(rec.id, rec.static_pubkey);
    if (!sig.ok()) {
        std::fprintf(stderr, "[rendezvous] sign failed for %s: %s\n", f.id.c_str(),
                     sig.status.message.c_str());
        return;
    }
    send_frame(sock, from, rendezvous::MessageType::RegisterOk, f.id,
               encode_attestation(rec.static_pubkey, sig.value, signer.verify_key()));
    std::printf("[rendezvous] REGISTER ok id=%s ep=%s live=%zu\n", f.id.c_str(),
                from.display().c_str(), reg.size());
}

void handle_lookup(net::Socket& sock, const net::Endpoint& from, const rendezvous::Frame& f,
                   rendezvous::RegistryStore& reg, rendezvous::Signer& signer) {
    // LOOKUP body holds the target REDesk-ID (also placed in f.id for symmetry).
    const rendezvous::RedeskId target =
        !f.id.empty() ? f.id
                      : std::string(reinterpret_cast<const char*>(f.body.data()), f.body.size());
    auto rec = reg.lookup(target);
    if (!rec) {
        send_frame(sock, from, rendezvous::MessageType::LookupFail, target,
                   {/* reason bytes — TODO: structured reason codes */});
        return;
    }
    auto sig = signer.sign_peer_key(rec->id, rec->static_pubkey);
    if (!sig.ok()) return;
    send_frame(sock, from, rendezvous::MessageType::LookupOk, target,
               encode_attestation(rec->static_pubkey, sig.value, signer.verify_key()));
    std::printf("[rendezvous] LOOKUP ok target=%s -> %s\n", target.c_str(),
                rec->endpoint.display().c_str());
}

void handle_broker(net::Socket& sock, const net::Endpoint& from, const rendezvous::Frame& f,
                   rendezvous::RegistryStore& reg,
                   std::unordered_map<std::string, BrokerSession>& sessions) {
    // BROKER_CANDIDATES: f.id is the target peer's REDesk-ID, f.body is the
    // OPAQUE ICE-candidate / SDP blob to relay verbatim. The server records a
    // session keyed by "caller|callee" and forwards the blob to the target's
    // last-known endpoint. It never inspects the blob.
    auto target = reg.lookup(f.id);
    if (!target) {
        send_frame(sock, from, rendezvous::MessageType::LookupFail, f.id, {});
        return;
    }
    const std::string token = from.display() + "|" + target->endpoint.display();
    auto& sess = sessions[token];
    if (sess.created_unix_s == 0) {
        sess.caller_ep = from;
        sess.callee = f.id;
        sess.callee_ep = target->endpoint;
        sess.created_unix_s = now_unix_s();
    }
    // Relay the opaque candidate blob to the target peer, tagging the sender's
    // endpoint so the far side can punch back. (Skeleton: forward body as-is.)
    send_frame(sock, target->endpoint, rendezvous::MessageType::BrokerCandidates, f.id, f.body);
    std::printf("[rendezvous] BROKER relay %s -> %s (%zu bytes)\n", from.display().c_str(),
                target->endpoint.display().c_str(), f.body.size());
    // TODO(ADR §3.3): if both peers signal punch failure, select and return the
    // lowest-RTT redesk-relay allocation here.
}

int run(std::uint16_t control_port) {
    std::printf("redesk-rendezvous %s — signaling/zero-knowledge (ADR §3.3/§3.6)\n",
                redesk::kVersion);

    net::NetInit net_guard;

    auto registry = rendezvous::make_in_memory_registry();

    // DEFAULT stub build: insecure deterministic signer. The real Ed25519 signer
    // (libsodium) is selected under REDESK_USE_REAL_BACKENDS with a persisted
    // key — TODO(ADR §3.6.1): load the seed from the OS keystore / config.
    auto signer = rendezvous::make_insecure_test_signer();
    if (!signer->is_secure()) {
        std::fprintf(stderr,
                     "[rendezvous] *** WARNING: using INSECURE-TEST-ONLY Ed25519 signer. "
                     "Signatures are FAKE. Build with REDESK_USE_REAL_BACKENDS for libsodium. ***\n");
    }

    auto udp = net::Socket::bind_udp(control_port);
    if (!udp.ok()) {
        std::fprintf(stderr, "[rendezvous] fatal: %s\n", udp.status.message.c_str());
        return 1;
    }
    // TCP/TLS-443 control path for UDP-blocked networks (§3.3). Bound but the
    // accept loop is left as a skeleton — TODO: integrate into the event loop.
    auto tcp = net::Socket::listen_tcp(control_port);
    if (!tcp.ok()) {
        std::fprintf(stderr, "[rendezvous] note: TCP listen unavailable (%s); UDP-only\n",
                     tcp.status.message.c_str());
    }
    std::printf("[rendezvous] listening control UDP/%u%s\n", control_port,
                tcp.ok() ? " + TCP" : "");

    std::unordered_map<std::string, BrokerSession> sessions;
    std::int64_t last_sweep = now_unix_s();
    std::vector<std::uint8_t> buf(64 * 1024);

    // Single-threaded poll loop. Stub-only: a real deployment uses a proper
    // readiness-multiplexed event loop (epoll/kqueue/IOCP) and a worker pool.
    // TODO(ADR §3.3): replace with the production event loop + graceful shutdown.
    for (;;) {
        net::Endpoint from;
        auto r = udp.value.recv_from(buf.data(), buf.size(), from);
        if (r.ok()) {
            auto frame = rendezvous::decode_frame(buf.data(), r.value);
            if (frame.ok() && frame.value.type != rendezvous::MessageType::Unknown) {
                const auto& f = frame.value;
                switch (f.type) {
                    case rendezvous::MessageType::Register:
                        handle_register(udp.value, from, f, *registry, *signer);
                        break;
                    case rendezvous::MessageType::Keepalive: {
                        // Refresh binding from observed source; ignore if unknown.
                        if (auto rec = registry->lookup(f.id)) {
                            rec->endpoint = from;
                            rec->last_seen_unix_s = now_unix_s();
                            registry->upsert(*rec);
                        }
                        break;
                    }
                    case rendezvous::MessageType::Lookup:
                        handle_lookup(udp.value, from, f, *registry, *signer);
                        break;
                    case rendezvous::MessageType::BrokerCandidates:
                        handle_broker(udp.value, from, f, *registry, sessions);
                        break;
                    default:
                        break; // server-origin types are never inbound
                }
            }
        } else if (r.status.code != ErrorCode::Again) {
            std::fprintf(stderr, "[rendezvous] recv error: %s\n", r.status.message.c_str());
        }

        const std::int64_t now = now_unix_s();
        if (now - last_sweep >= kExpirySweepSeconds) {
            const std::size_t removed = registry->expire(now, kRegistryTtlSeconds);
            if (removed) std::printf("[rendezvous] expired %zu stale bindings\n", removed);
            // Drop broker sessions older than the registry TTL too.
            for (auto it = sessions.begin(); it != sessions.end();) {
                it = (now - it->second.created_unix_s > kRegistryTtlSeconds)
                         ? sessions.erase(it)
                         : std::next(it);
            }
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
    return kDefaultControlPort;
}

} // namespace

int main(int argc, char** argv) {
    return run(parse_port(argc, argv));
}
