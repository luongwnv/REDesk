// In-process loopback transport + a minimal GCC-shaped congestion controller
// (ADR-001 §3.3). Compiled by default (REDESK_USE_REAL_BACKENDS=OFF).
//
// The loopback delivers each send() synchronously to the peer's receive callback
// so the full session/crypto/codec pipeline runs end-to-end with no sockets, no
// NAT, no threads. Reliability is modeled per channel: the reliable channels
// always deliver; the unreliable (media) channels also deliver in this stub
// (a network simulator under tests/ injects loss/jitter for CC work).
//
// TODO(ADR §3.3): the real backend is custom UDP over a libjuice ICE socket with
// FEC + selective-NACK on media, ordered ARQ per reliable channel, a MANDATORY
// packet pacer + keyframe-size governor, and the rendezvous/relay topology.

#include "core/transport/transport.h"

#include <algorithm>
#include <memory>

#include "core/common/logging.h"

namespace redesk::transport {

const char* toString(Channel ch) noexcept {
    switch (ch) {
        case Channel::Video:        return "Video";
        case Channel::Audio:        return "Audio";
        case Channel::Input:        return "Input";
        case Channel::Control:      return "Control";
        case Channel::Clipboard:    return "Clipboard";
        case Channel::FileTransfer: return "FileTransfer";
    }
    return "?";
}

Reliability reliabilityFor(Channel ch) noexcept {
    switch (ch) {
        case Channel::Video:
        case Channel::Audio:
            return Reliability::Unreliable;
        case Channel::Input:
        case Channel::Control:
        case Channel::Clipboard:
        case Channel::FileTransfer:
            return Reliability::ReliableOrdered;
    }
    return Reliability::ReliableOrdered;
}

namespace {

// One end of a loopback pair. `peer_` is set by createLoopbackPair(); send()
// hands the payload straight to the peer's receive callback. Not thread-safe by
// design — the loopback runs on the test's calling thread.
class LoopbackTransport final : public ITransport {
public:
    void setReceiveCallback(ReceiveCallback cb) override {
        receive_cb_ = std::move(cb);
    }
    void setStateCallback(StateCallback cb) override {
        state_cb_ = std::move(cb);
    }

    Status connect(const PeerEndpoint& peer) override {
        (void)peer;
        // Loopback is already wired; transition straight to Connected.
        setState(TransportState::Connected);
        return Status::success();
    }

    Status send(Channel channel, const std::vector<uint8_t>& payload) override {
        if (state_ != TransportState::Connected) {
            return Status::error(ErrorCode::ConnectionLost, "not connected");
        }
        // §3.6: oversized payloads must be fragmented above this layer.
        if (payload.size() > kMaxTransportMessage) {
            return Status::error(ErrorCode::InvalidArgument,
                                 "payload exceeds 65535-byte transport cap");
        }
        LoopbackTransport* peer = peer_;
        if (!peer || !peer->receive_cb_) {
            // Unreliable channels silently drop with no peer; reliable channels
            // surface the loss so callers notice a broken wiring in tests.
            if (reliabilityFor(channel) == Reliability::ReliableOrdered) {
                return Status::error(ErrorCode::ConnectionLost,
                                     "peer has no receiver");
            }
            return Status::success();
        }
        // Deliver synchronously (zero-latency model). Copy so the callback owns it.
        std::vector<uint8_t> copy = payload;
        peer->receive_cb_(channel, copy);
        return Status::success();
    }

    void close() override { setState(TransportState::Closed); }
    TransportState state() const override { return state_; }

    void bindPeer(LoopbackTransport* peer) { peer_ = peer; }

private:
    void setState(TransportState s) {
        if (state_ == s) {
            return;
        }
        state_ = s;
        if (state_cb_) {
            state_cb_(s);
        }
    }

    static constexpr size_t kMaxTransportMessage = 65535;

    ReceiveCallback receive_cb_;
    StateCallback state_cb_;
    LoopbackTransport* peer_ = nullptr;
    TransportState state_ = TransportState::Idle;
};

// Minimal GCC-shaped controller. NOT a real trendline/Kalman estimator — it is a
// conservative AIMD over the delay-gradient + loss signals, enough to drive the
// encoder in tests. The real controller seeds from a known-good GCC (§7 risk 1).
class StubCongestionController final : public ICongestionController {
public:
    void onFeedback(const FeedbackReport& r) override {
        // Delay-based gate: rising queuing delay => multiplicative decrease.
        // Falling/low delay + low loss => additive increase. Loss is a hard cap.
        const double loss_ratio =
            r.received_packets > 0
                ? static_cast<double>(r.lost_packets) /
                      static_cast<double>(r.lost_packets + r.received_packets)
                : 0.0;

        if (r.queuing_delay_us > kDelayOveruseUs || loss_ratio > kLossHigh) {
            target_kbps_ = static_cast<uint32_t>(target_kbps_ * 0.85); // GCC beta
        } else if (loss_ratio < kLossLow) {
            target_kbps_ += kAdditiveIncreaseKbps;
        }
        target_kbps_ = std::clamp(target_kbps_, min_kbps_, max_kbps_);
    }

    uint32_t targetBitrateKbps() const override { return target_kbps_; }

    void setBitrateBounds(uint32_t min_kbps, uint32_t max_kbps) override {
        min_kbps_ = min_kbps;
        max_kbps_ = std::max(max_kbps, min_kbps);
        target_kbps_ = std::clamp(target_kbps_, min_kbps_, max_kbps_);
    }

private:
    static constexpr int64_t kDelayOveruseUs = 30'000; // 30 ms gradient threshold
    static constexpr double kLossHigh = 0.10;
    static constexpr double kLossLow = 0.02;
    static constexpr uint32_t kAdditiveIncreaseKbps = 500;

    uint32_t min_kbps_ = 500;
    uint32_t max_kbps_ = 50'000;
    uint32_t target_kbps_ = 8'000;
};

} // namespace

std::unique_ptr<ITransport> createTransport() {
#if defined(REDESK_USE_REAL_BACKENDS)
    // TODO(ADR §3.3): return the libjuice/ICE + custom-UDP transport here.
    REDESK_LOG(Warn, "transport")
        << "real transport not yet implemented; using loopback half";
    return std::make_unique<LoopbackTransport>();
#else
    // A lone loopback endpoint with no peer is only useful via createLoopbackPair.
    return std::make_unique<LoopbackTransport>();
#endif
}

std::unique_ptr<ICongestionController> createCongestionController() {
    return std::make_unique<StubCongestionController>();
}

LoopbackPair createLoopbackPair() {
    auto a = std::make_unique<LoopbackTransport>();
    auto b = std::make_unique<LoopbackTransport>();
    a->bindPeer(b.get());
    b->bindPeer(a.get());
    return LoopbackPair{std::move(a), std::move(b)};
}

} // namespace redesk::transport
