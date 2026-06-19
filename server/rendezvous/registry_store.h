#pragma once

// REDesk-ID -> endpoint registry (ADR-001 §2, §3.3).
//
// Maps a REDesk-ID to its last-known reachable endpoint, X25519 static pubkey,
// and last-seen time. This is the rendezvous server's only persistent-ish state
// and it is deliberately thin: the server is signaling, not a directory of
// secrets. It NEVER stores private keys or media keys (§3.6.2).
//
// Interface + in-memory stub here. The real build swaps in a sharded/replicated
// store (Redis / a CRDT registry / consistent-hash ring) so the server is
// horizontally scalable and multi-region (§3.3), gated behind
// REDESK_USE_REAL_BACKENDS. TODO(ADR §3.3): real RegistryStore backend.

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/common/types.h"
#include "server/common/net.h"
#include "server/rendezvous/protocol.h"

namespace redesk::server::rendezvous {

// One registry entry. `last_seen_unix_s` drives TTL expiry of stale NAT
// bindings; a peer must re-REGISTER / Keepalive before its binding expires.
struct PeerRecord {
    RedeskId id;
    net::Endpoint endpoint;        // observed packet source (anti-spoof, §protocol)
    StaticPubKey static_pubkey;    // 32B X25519 (§3.6.1)
    std::uint32_t capabilities = 0;
    std::int64_t last_seen_unix_s = 0;
};

// Abstract registry. Implementations must be safe to call from the listener
// thread(s); the stub serializes internally.
class RegistryStore {
public:
    virtual ~RegistryStore() = default;

    // Insert or refresh a peer (REGISTER / Keepalive). Overwrites endpoint +
    // last_seen; rejects a static_pubkey change for an existing live id unless
    // the prior binding is already expired (basic squatting guard — real fleets
    // also layer per-ID auth + key pinning, §3.6.6).
    virtual Status upsert(const PeerRecord& rec) = 0;

    // Resolve a REDesk-ID (LOOKUP). nullopt if unknown or expired.
    virtual std::optional<PeerRecord> lookup(const RedeskId& id) const = 0;

    // Drop entries whose last_seen is older than ttl_seconds relative to
    // now_unix_s. Returns count removed. Called periodically by the server.
    virtual std::size_t expire(std::int64_t now_unix_s, std::int64_t ttl_seconds) = 0;

    // Current live-entry count (metrics / capacity planning).
    virtual std::size_t size() const = 0;
};

// In-memory stub backing store. No external deps; suitable for single-node
// dev / tests. Thread-safe.
std::unique_ptr<RegistryStore> make_in_memory_registry();

} // namespace redesk::server::rendezvous
