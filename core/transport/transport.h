#pragma once

// Network transport abstraction + congestion control surface (ADR-001 §3.3).
//
// One custom UDP transport over an ICE-managed socket (libjuice in the real
// backend); NOT libwebrtc media, NOT QUIC for the primary video path. All
// channels are multiplexed over a single UDP 5-tuple with PER-CHANNEL reliability:
//
//   Channel       Reliability                              Rationale (§3.3)
//   ----------    --------------------------------------   ---------------------------
//   Video         Unreliable / partially-reliable          per-frame deadline, drop stale;
//                                                           FEC + tight selective-NACK
//   Audio         Unreliable / partially-reliable          drop-late, low latency
//   Input         Reliable, ordered, IMMEDIATE ARQ         must never be lost or reordered
//   Control       Reliable, ordered                        session/capability messages
//   Clipboard     Reliable, ordered                        text/image/uri-list sync
//   FileTransfer  Reliable, ordered (own stream)           bulk; MUST NOT head-of-line
//                                                           block Input (independent streams)
//
// CongestionController is delay-based GCC (§3.3): onFeedback() consumes
// transport-wide receive reports, targetBitrate() drives the encoder per RTT.
// A Pacer (note below) sits between encoder and socket and is MANDATORY in the
// real backend — it smooths large keyframes so GCC doesn't mis-measure queuing
// delay and over-throttle ("judder on screen change"). The stub omits real
// pacing but documents where it belongs.
//
// The default STUB (REDESK_USE_REAL_BACKENDS=OFF) is an IN-PROCESS LOOPBACK pair:
// two LoopbackTransport endpoints wired to each other so session/crypto/codec
// tests run end-to-end without sockets, NAT, or threads.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/common/types.h"

namespace redesk::transport {

// Multiplexed logical channels (§3.3). The enum value order is stable wire-side.
enum class Channel : uint8_t {
    Video = 0,
    Audio = 1,
    Input = 2,
    Control = 3,
    Clipboard = 4,
    FileTransfer = 5,
};

const char* toString(Channel ch) noexcept;

// Per-channel delivery contract. Real backends map these onto FEC/NACK (media)
// vs ordered ARQ (the reliable channels); the loopback stub honors ordering and
// always-deliver for the reliable channels and best-effort for media.
enum class Reliability {
    Unreliable,        // drop-late, no retransmit (Video/Audio)
    ReliableOrdered,   // never drop, in-order, independent stream
};

// Returns the §3.3 reliability semantics for a channel.
Reliability reliabilityFor(Channel ch) noexcept;

enum class TransportState {
    Idle,
    Connecting,        // ICE gathering / hole-punch (real backend)
    Connected,
    Failed,
    Closed,
};

// Inbound datagram callback: (channel, payload). Payload is owned by the
// callback for its duration only.
using ReceiveCallback =
    std::function<void(Channel, const std::vector<uint8_t>&)>;
using StateCallback = std::function<void(TransportState)>;

// Endpoint description for connect(). In the real backend this is brokered via
// the rendezvous server (ICE candidates); the loopback stub ignores it.
struct PeerEndpoint {
    std::string peer_id;       // REDesk routing handle (not the identity)
    std::string remote_addr;   // ip:port (direct) — empty => use signaling/relay
};

class ITransport {
public:
    virtual ~ITransport() = default;

    virtual void setReceiveCallback(ReceiveCallback cb) = 0;
    virtual void setStateCallback(StateCallback cb) = 0;

    // Begin connecting to `peer`. Asynchronous in real backends (state callback
    // reports progress). The loopback stub transitions to Connected immediately.
    virtual Status connect(const PeerEndpoint& peer) = 0;

    // Send `payload` on `channel`. Honors that channel's reliability contract.
    // Oversized payloads must be fragmented above this layer (§3.6: respect the
    // 65535-byte Noise transport-message cap) — transports may reject them.
    virtual Status send(Channel channel, const std::vector<uint8_t>& payload) = 0;

    virtual void close() = 0;
    virtual TransportState state() const = 0;
};

// ---------------------------------------------------------------------------
// Congestion control (§3.3). Delay-based GCC: feed it per-packet receive
// feedback; read a target send bitrate that drives the encoder. NOT BBR for
// interactive video (BBR is reserved for the bulk file-transfer channel).
// ---------------------------------------------------------------------------
struct FeedbackReport {
    uint64_t now_us = 0;            // local time the report was processed
    uint32_t acked_bytes = 0;      // bytes the receiver acknowledged this report
    uint32_t lost_packets = 0;     // loss observed this interval
    uint32_t received_packets = 0; // delivered this interval
    int64_t  rtt_us = 0;           // current smoothed RTT estimate
    int64_t  queuing_delay_us = 0; // GCC delay-gradient signal (trendline/Kalman)
};

class ICongestionController {
public:
    virtual ~ICongestionController() = default;

    // Consume one transport-wide feedback report; updates the estimate.
    virtual void onFeedback(const FeedbackReport& report) = 0;

    // Current target send bitrate in kbps. The encoder retargets to this (§3.3).
    virtual uint32_t targetBitrateKbps() const = 0;

    // Bounds the controller's output (link floor/ceiling, app policy).
    virtual void setBitrateBounds(uint32_t min_kbps, uint32_t max_kbps) = 0;
};

// Factories. Real backend (libjuice ICE + custom UDP) when
// REDESK_USE_REAL_BACKENDS=ON, else the in-process loopback / simple GCC stub.
std::unique_ptr<ITransport> createTransport();
std::unique_ptr<ICongestionController> createCongestionController();

// ---------------------------------------------------------------------------
// In-process loopback (test-only). Creates a connected pair of endpoints whose
// sends are delivered to the other's receive callback, respecting per-channel
// reliability (reliable channels always deliver in order). No real network.
//
// Pacer note: a production Pacer + keyframe-size governor sits between the
// encoder and ITransport::send for the Video channel (§3.3). The loopback stub
// delivers immediately and therefore models a zero-latency, infinite-bandwidth
// link — fine for correctness tests, useless for CC tuning (use the network sim
// under tests/ for that).
// ---------------------------------------------------------------------------
struct LoopbackPair {
    std::unique_ptr<ITransport> a;
    std::unique_ptr<ITransport> b;
};
LoopbackPair createLoopbackPair();

} // namespace redesk::transport
