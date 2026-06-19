// In-memory relay allocation table (ADR-001 §3.3, §3.6.2).
//
// Single-node, dependency-free, thread-safe. The relay never decrypts; this
// table only pairs two endpoints by token and counts bytes. The real backend
// is sharded/anycast (see header TODO).

#include "server/relay/allocation_table.h"

#include <mutex>
#include <unordered_map>

namespace redesk::server::relay {

namespace {

class InMemoryAllocationTable final : public AllocationTable {
public:
    Status allocate(const SessionToken& token, std::int64_t now_unix_s) override {
        if (token.empty()) {
            return Status::error(ErrorCode::InvalidArgument, "empty session token");
        }
        std::lock_guard<std::mutex> lk(mu_);
        auto& alloc = map_[token];
        if (alloc.created_unix_s == 0) {
            alloc.token = token;
            alloc.created_unix_s = now_unix_s;
            alloc.last_activity_unix_s = now_unix_s;
        }
        return Status::success();
    }

    Result<net::Endpoint> route(const SessionToken& token, const net::Endpoint& src,
                                std::size_t bytes, std::int64_t now_unix_s) override {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = map_.find(token);
        if (it == map_.end()) {
            return Result<net::Endpoint>::fail(ErrorCode::InvalidArgument, "unknown session token");
        }
        Allocation& al = it->second;
        al.last_activity_unix_s = now_unix_s;

        Leg* self = nullptr;
        Leg* peer = nullptr;

        // Match src to an already-bound leg first, else claim a free leg. This is
        // the relay's address-learning step (symmetric to ICE discovery): the
        // first datagram from each side teaches the relay that side's endpoint.
        if (al.a.bound && al.a.endpoint == src) {
            self = &al.a;
            peer = &al.b;
        } else if (al.b.bound && al.b.endpoint == src) {
            self = &al.b;
            peer = &al.a;
        } else if (!al.a.bound) {
            al.a.endpoint = src;
            al.a.bound = true;
            self = &al.a;
            peer = &al.b;
        } else if (!al.b.bound) {
            al.b.endpoint = src;
            al.b.bound = true;
            self = &al.b;
            peer = &al.a;
        } else {
            // Both legs bound to other addresses — a third party on this token.
            // Drop (do not forward) to avoid being an open reflector.
            return Result<net::Endpoint>::fail(ErrorCode::PermissionDenied,
                                               "session legs already bound to other peers");
        }

        self->bytes_forwarded += bytes;
        if (!peer->bound) {
            // Peer leg hasn't spoken yet; nothing to forward to.
            return Result<net::Endpoint>::fail(ErrorCode::Again, "peer leg not yet bound");
        }
        return Result<net::Endpoint>::good(peer->endpoint);
    }

    std::size_t expire(std::int64_t now_unix_s, std::int64_t ttl_seconds) override {
        std::lock_guard<std::mutex> lk(mu_);
        std::size_t removed = 0;
        for (auto it = map_.begin(); it != map_.end();) {
            if (now_unix_s - it->second.last_activity_unix_s > ttl_seconds) {
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
    mutable std::mutex mu_;
    std::unordered_map<SessionToken, Allocation> map_;
};

} // namespace

std::unique_ptr<AllocationTable> make_in_memory_allocation_table() {
    return std::make_unique<InMemoryAllocationTable>();
}

} // namespace redesk::server::relay
