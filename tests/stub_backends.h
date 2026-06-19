#pragma once

// REDesk test reference stubs.
//
// ADR-001 §3.2 (codec), §3.3 (transport), §3.6 (crypto + permissions).
//
// =============================== READ ME ===================================
// These are SELF-CONTAINED reference stubs used by the test suite to prove the
// scaffold wiring and the protocol contract WITHOUT depending on the exact API
// shape of the (parallel-authored) core/ stub backends.
//
// They intentionally mirror the interface shapes that core/ is expected to
// expose so the integrator can do a 1:1 swap. Each block documents the
// "EXPECTED CORE API" it stands in for. When core/codec, core/transport,
// core/crypto, and core/session land with confirmed signatures, replace the
// corresponding section here with:
//     #include "core/codec/encoder.h"     // makeStubEncoder() / makeStubDecoder()
//     #include "core/transport/transport.h"
//     #include "core/crypto/noise_session.h"
//     #include "core/session/permission_gate.h"
// and delete the matching reference stub. The tests are written against the
// behavior the ADR mandates, not against these specific classes, so the swap is
// mechanical.
//
// Everything here is namespace redesk::test::stub, dependency-free (std + proto
// + core/common/types.h only), and compiles in the stub build on any host.
// ===========================================================================

#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/common/types.h"
#include "proto/codec.proto.h"
#include "proto/ipc.proto.h"
#include "proto/transport.proto.h"

namespace redesk::test::stub {

// ===========================================================================
// (1) CODEC stubs — EXPECTED CORE API: redesk::codec::IVideoEncoder /
//     IVideoDecoder pure-virtual interfaces + makeStubEncoder()/makeStubDecoder()
//     factories (ADR §3.2). The stub encode/decode is a lossless identity
//     transform with a tiny container header so a decoder can recover the exact
//     frame a real HW path would have round-tripped.
// ===========================================================================

// Marker so the decoder can sanity-check it received our stub container.
inline constexpr uint32_t kStubCodecMagic = 0x52455343; // 'RESC'

class IVideoEncoder {
public:
    virtual ~IVideoEncoder() = default;
    // Encode one frame into an EncodedPacket. The stub is lossless identity:
    // it serializes the frame's geometry + pixels so the decoder can rebuild it.
    virtual Result<EncodedPacket> encode(const VideoFrame& frame) = 0;
};

class IVideoDecoder {
public:
    virtual ~IVideoDecoder() = default;
    virtual Result<VideoFrame> decode(const EncodedPacket& packet) = 0;
};

class StubEncoder final : public IVideoEncoder {
public:
    Result<EncodedPacket> encode(const VideoFrame& frame) override {
        // TODO(ADR §3.2): real path = HW NVENC/AMF/QSV/VideoToolbox/VAAPI encode
        // of a GPU-resident NV12/P010/I444 texture; here we losslessly pack the
        // CPU pixels so the round-trip is verifiable in the dep-free build.
        EncodedPacket pkt;
        pkt.timestamp_us = frame.timestamp_us;
        pkt.keyframe = true; // every stub frame is independently decodable
        auto& d = pkt.data;
        auto put32 = [&d](uint32_t v) {
            d.push_back(uint8_t(v & 0xff));
            d.push_back(uint8_t((v >> 8) & 0xff));
            d.push_back(uint8_t((v >> 16) & 0xff));
            d.push_back(uint8_t((v >> 24) & 0xff));
        };
        put32(kStubCodecMagic);
        put32(frame.size.width);
        put32(frame.size.height);
        put32(static_cast<uint32_t>(frame.format));
        put32(static_cast<uint32_t>(frame.cpu_pixels.size()));
        d.insert(d.end(), frame.cpu_pixels.begin(), frame.cpu_pixels.end());
        return Result<EncodedPacket>::good(std::move(pkt));
    }
};

class StubDecoder final : public IVideoDecoder {
public:
    Result<VideoFrame> decode(const EncodedPacket& packet) override {
        const auto& d = packet.data;
        if (d.size() < 20) {
            return Result<VideoFrame>::fail(ErrorCode::InvalidArgument,
                                            "stub packet too small");
        }
        size_t off = 0;
        auto get32 = [&d, &off]() -> uint32_t {
            uint32_t v = uint32_t(d[off]) | (uint32_t(d[off + 1]) << 8) |
                         (uint32_t(d[off + 2]) << 16) | (uint32_t(d[off + 3]) << 24);
            off += 4;
            return v;
        };
        const uint32_t magic = get32();
        if (magic != kStubCodecMagic) {
            return Result<VideoFrame>::fail(ErrorCode::InvalidArgument,
                                            "stub codec magic mismatch");
        }
        VideoFrame f;
        f.size.width = get32();
        f.size.height = get32();
        f.format = static_cast<PixelFormat>(get32());
        const uint32_t n = get32();
        if (off + n != d.size()) {
            return Result<VideoFrame>::fail(ErrorCode::InvalidArgument,
                                            "stub payload length mismatch");
        }
        f.cpu_pixels.assign(d.begin() + off, d.end());
        f.timestamp_us = packet.timestamp_us;
        return Result<VideoFrame>::good(std::move(f));
    }
};

inline std::unique_ptr<IVideoEncoder> makeStubEncoder() {
    return std::make_unique<StubEncoder>();
}
inline std::unique_ptr<IVideoDecoder> makeStubDecoder() {
    return std::make_unique<StubDecoder>();
}

// ===========================================================================
// (2) CRYPTO stubs — EXPECTED CORE API: redesk::crypto::INoiseSession +
//     makeStubInitiator()/makeStubResponder() (ADR §3.6). The stub completes a
//     toy XK-style handshake and provides INSECURE xor "AEAD" so the transport
//     round-trip is testable with no libsodium. The real path is Noise_XK/KK on
//     libsodium with ChaCha20-Poly1305.
// ===========================================================================

enum class HandshakeRole : uint8_t { kInitiator, kResponder };

class INoiseSession {
public:
    virtual ~INoiseSession() = default;
    // Drive the handshake: feed the peer's last message (empty to start), get
    // the next message to send (empty when nothing more to send). When both
    // sides report established(), the transport keys are ready.
    virtual Result<std::vector<uint8_t>> writeHandshake(
        const std::vector<uint8_t>& incoming) = 0;
    virtual bool established() const = 0;
    // Channel-binding value (handshake hash) — auth (CPace/OPAQUE) binds to it
    // (ADR §3.6 #4). Available only after established().
    virtual std::vector<uint8_t> handshakeHash() const = 0;
    // Transport AEAD after handshake. INSECURE xor stub; real = ChaCha20-Poly1305.
    virtual std::vector<uint8_t> encrypt(const std::vector<uint8_t>& plaintext) = 0;
    virtual Result<std::vector<uint8_t>> decrypt(
        const std::vector<uint8_t>& ciphertext) = 0;
};

// A trivially "correct" toy handshake: initiator and responder each send one
// message; once both have been exchanged, both derive the same symmetric key by
// hashing the concatenation of the two messages. This is NOT secure — it exists
// only to prove the session lifecycle + AEAD round-trip wiring.
class StubNoiseSession final : public INoiseSession {
public:
    explicit StubNoiseSession(HandshakeRole role) : role_(role) {}

    Result<std::vector<uint8_t>> writeHandshake(
        const std::vector<uint8_t>& incoming) override {
        // INSECURE toy XK: initiator speaks first; responder replies; then both
        // mix the transcript into a session key.
        if (!incoming.empty()) {
            transcript_.insert(transcript_.end(), incoming.begin(), incoming.end());
            ++rounds_seen_;
        }
        std::vector<uint8_t> out;
        if (role_ == HandshakeRole::kInitiator) {
            if (sent_ == 0) {
                out = {'X', 'K', 'i', static_cast<uint8_t>(role_)};
                transcript_.insert(transcript_.end(), out.begin(), out.end());
                ++sent_;
            } else if (rounds_seen_ >= 1) {
                established_ = true;  // initiator established after responder reply
            }
        } else {  // responder
            if (rounds_seen_ >= 1 && sent_ == 0) {
                out = {'X', 'K', 'r', static_cast<uint8_t>(role_)};
                transcript_.insert(transcript_.end(), out.begin(), out.end());
                ++sent_;
                established_ = true;  // responder established after sending reply
            }
        }
        if (established_) deriveKey();
        return Result<std::vector<uint8_t>>::good(std::move(out));
    }

    bool established() const override { return established_; }

    std::vector<uint8_t> handshakeHash() const override { return hash_; }

    std::vector<uint8_t> encrypt(const std::vector<uint8_t>& pt) override {
        // TODO(ADR §3.6): real AEAD = ChaCha20-Poly1305 with per-message nonce.
        std::vector<uint8_t> ct(pt.size());
        for (size_t i = 0; i < pt.size(); ++i) {
            ct[i] = pt[i] ^ key_[i % key_.size()] ^ static_cast<uint8_t>(send_nonce_);
        }
        // Prepend the nonce so the peer can reverse it; real Noise tracks nonces
        // implicitly. INSECURE — illustrative only.
        ct.insert(ct.begin(), static_cast<uint8_t>(send_nonce_));
        ++send_nonce_;
        return ct;
    }

    Result<std::vector<uint8_t>> decrypt(const std::vector<uint8_t>& ct) override {
        if (ct.empty()) {
            return Result<std::vector<uint8_t>>::fail(ErrorCode::InvalidArgument,
                                                      "empty ciphertext");
        }
        const uint8_t nonce = ct[0];
        std::vector<uint8_t> pt(ct.size() - 1);
        for (size_t i = 0; i < pt.size(); ++i) {
            pt[i] = ct[i + 1] ^ key_[i % key_.size()] ^ nonce;
        }
        return Result<std::vector<uint8_t>>::good(std::move(pt));
    }

private:
    void deriveKey() {
        if (!key_.empty()) return;
        // Toy FNV-1a-ish digest over the transcript -> 16-byte key + hash.
        uint64_t h = 1469598103934665603ull;
        for (uint8_t b : transcript_) {
            h ^= b;
            h *= 1099511628211ull;
        }
        key_.resize(16);
        hash_.resize(32);
        for (int i = 0; i < 16; ++i) key_[i] = uint8_t((h >> ((i % 8) * 8)) & 0xff);
        for (int i = 0; i < 32; ++i) {
            h ^= (h >> 7);
            h *= 1099511628211ull;
            hash_[i] = uint8_t(h & 0xff);
        }
    }

    HandshakeRole role_;
    std::vector<uint8_t> transcript_;
    std::vector<uint8_t> key_;
    std::vector<uint8_t> hash_;
    bool established_ = false;
    int rounds_seen_ = 0;
    int sent_ = 0;
    uint32_t send_nonce_ = 0;
};

inline std::unique_ptr<INoiseSession> makeStubInitiator() {
    return std::make_unique<StubNoiseSession>(HandshakeRole::kInitiator);
}
inline std::unique_ptr<INoiseSession> makeStubResponder() {
    return std::make_unique<StubNoiseSession>(HandshakeRole::kResponder);
}

// ===========================================================================
// (3) TRANSPORT loopback — EXPECTED CORE API: redesk::transport::ITransport
//     with send(Channel, MessageType, bytes) + a per-channel receive callback,
//     honoring proto::reliabilityFor() (ADR §3.3). The in-process loopback
//     delivers frames straight to the peer's channel handlers. Reliable-ordered
//     channels preserve order; an unreliable channel may drop (we model a
//     deterministic drop to prove drop-late semantics).
// ===========================================================================

struct DeliveredFrame {
    proto::FrameHeader header;
    std::vector<uint8_t> payload;
};

using FrameHandler = std::function<void(const DeliveredFrame&)>;

class ITransport {
public:
    virtual ~ITransport() = default;
    virtual void onChannel(proto::Channel ch, FrameHandler handler) = 0;
    virtual Status send(proto::Channel ch, proto::MessageType type,
                        std::vector<uint8_t> payload,
                        uint16_t flags = proto::kFlagNone) = 0;
};

// In-process loopback: two endpoints wired to each other. Anything one side
// sends is delivered to the other side's matching channel handler. A reliable
// channel buffers + flushes in FIFO order; an unreliable channel can be told to
// drop the next N frames to exercise drop-late behavior.
class LoopbackTransport final : public ITransport {
public:
    void onChannel(proto::Channel ch, FrameHandler handler) override {
        handlers_[ch] = std::move(handler);
    }

    Status send(proto::Channel ch, proto::MessageType type,
                std::vector<uint8_t> payload,
                uint16_t flags = proto::kFlagNone) override {
        if (!peer_) {
            return Status::error(ErrorCode::ConnectionLost, "loopback not linked");
        }
        proto::FrameHeader h;
        h.channel = ch;
        h.type = type;
        h.flags = flags;
        h.seq = next_seq_[ch]++;
        h.length = static_cast<uint32_t>(payload.size());

        // Honor reliability policy (ADR §3.3): unreliable channels may drop;
        // reliable channels are delivered in order.
        if (proto::reliabilityFor(ch) == proto::Reliability::kUnreliable &&
            peer_->drop_budget_[ch] > 0) {
            --peer_->drop_budget_[ch];
            return Status::success();  // dropped on purpose; no retransmit
        }
        peer_->deliver(DeliveredFrame{h, std::move(payload)});
        return Status::success();
    }

    // Tell THIS endpoint to drop the next `n` inbound frames on `ch` (only
    // meaningful for unreliable channels; reliable channels ignore the budget).
    void dropNextInbound(proto::Channel ch, int n) { drop_budget_[ch] = n; }

    static void link(LoopbackTransport& a, LoopbackTransport& b) {
        a.peer_ = &b;
        b.peer_ = &a;
    }

private:
    void deliver(DeliveredFrame f) {
        auto it = handlers_.find(f.header.channel);
        if (it != handlers_.end() && it->second) it->second(f);
    }

    LoopbackTransport* peer_ = nullptr;
    std::map<proto::Channel, FrameHandler> handlers_;
    std::map<proto::Channel, uint32_t> next_seq_;
    std::map<proto::Channel, int> drop_budget_;
};

// ===========================================================================
// (4) SESSION permission gate — EXPECTED CORE API:
//     redesk::session::PermissionGate (default-deny capability gate keyed by
//     peer fingerprint) + an audit log keyed by fingerprint (ADR §3.6 #6).
// ===========================================================================

struct AuditEntry {
    std::string fingerprint;          // BLAKE2b safety number = identity
    proto::Capability capability;
    bool allowed = false;
    std::string action;               // "check" / "grant" / "revoke"
};

class PermissionGate {
public:
    // Default-deny: nothing is granted until grant() is called for a session.
    void grant(const std::string& fingerprint, proto::Capability cap) {
        grants_[fingerprint].grant(cap);
        audit_.push_back({fingerprint, cap, true, "grant"});
    }
    void revoke(const std::string& fingerprint, proto::Capability cap) {
        auto it = grants_.find(fingerprint);
        if (it != grants_.end()) it->second.revoke(cap);
        audit_.push_back({fingerprint, cap, false, "revoke"});
    }

    // Authoritative check. Records every decision in the audit log keyed by
    // fingerprint (ADR §3.6 #6: "audit log keyed by fingerprint").
    bool check(const std::string& fingerprint, proto::Capability cap) {
        bool allowed = false;
        auto it = grants_.find(fingerprint);
        if (it != grants_.end()) allowed = it->second.has(cap);
        audit_.push_back({fingerprint, cap, allowed, "check"});
        return allowed;
    }

    const std::vector<AuditEntry>& audit() const { return audit_; }

private:
    std::map<std::string, proto::CapabilitySet> grants_;
    std::vector<AuditEntry> audit_;
};

} // namespace redesk::test::stub
