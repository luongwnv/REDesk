// In-memory RegistryStore stub (ADR-001 §3.3).
//
// Single-node, dependency-free. Thread-safe via a coarse mutex — fine for the
// stub/dev path; the real store is sharded/replicated (see header TODO).

#include "server/rendezvous/registry_store.h"

#include <mutex>
#include <unordered_map>

namespace redesk::server::rendezvous {

namespace {

class InMemoryRegistry final : public RegistryStore {
public:
    Status upsert(const PeerRecord& rec) override {
        if (rec.id.empty()) {
            return Status::error(ErrorCode::InvalidArgument, "empty REDesk-ID");
        }
        if (rec.static_pubkey.size() != kStaticPubKeyLen) {
            return Status::error(ErrorCode::InvalidArgument,
                                 "static pubkey must be 32 bytes (X25519)");
        }
        std::lock_guard<std::mutex> lk(mu_);
        auto it = map_.find(rec.id);
        if (it != map_.end()) {
            const bool prior_live = rec.last_seen_unix_s - it->second.last_seen_unix_s < kSquatGuardWindowS;
            const bool key_changed = it->second.static_pubkey != rec.static_pubkey;
            if (prior_live && key_changed) {
                // Anti-squat: a live id cannot be hijacked with a different key
                // here. The *client* still independently verifies the server's
                // Ed25519 signature + TOFU key-change alert (§3.6.3); this is
                // only a coarse server-side guard.
                return Status::error(ErrorCode::PermissionDenied,
                                     "REDesk-ID held by a live peer with a different key");
            }
        }
        map_[rec.id] = rec;
        return Status::success();
    }

    std::optional<PeerRecord> lookup(const RedeskId& id) const override {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = map_.find(id);
        if (it == map_.end()) return std::nullopt;
        return it->second;
    }

    std::size_t expire(std::int64_t now_unix_s, std::int64_t ttl_seconds) override {
        std::lock_guard<std::mutex> lk(mu_);
        std::size_t removed = 0;
        for (auto it = map_.begin(); it != map_.end();) {
            if (now_unix_s - it->second.last_seen_unix_s > ttl_seconds) {
                it = map_.erase(it);
                ++removed;
            } else {
                ++it;
            }
        }
        return removed;
    }

    std::size_t size() const override {
        std::lock_guard<std::mutex> lk(mu_);
        return map_.size();
    }

private:
    // Window within which an existing binding counts as "live" for the squat
    // guard. Kept conservative; real TTL policy is operator-configured.
    static constexpr std::int64_t kSquatGuardWindowS = 90;

    mutable std::mutex mu_;
    std::unordered_map<RedeskId, PeerRecord> map_;
};

} // namespace

std::unique_ptr<RegistryStore> make_in_memory_registry() {
    return std::make_unique<InMemoryRegistry>();
}

} // namespace redesk::server::rendezvous
