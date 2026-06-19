#pragma once

// REDesk wire protocol — transport framing.
//
// ADR-001 §3.3 (Transport + NAT) and §3.6 (Security).
//
// This header is the *single source of truth* for the on-the-wire framing that
// rides inside the Noise-encrypted UDP channel. It is intentionally a plain,
// dependency-free C++ header (namespace redesk::proto) rather than a .proto /
// gRPC schema: the default stub build must have zero external dependencies
// (no protobuf, no grpc). A protobuf schema can later be *generated from* these
// definitions; until then these structs are the contract consumed by
// core/transport, core/crypto, service/, and ui/.
//
// Framing model (ADR §3.3, §3.6):
//   * One UDP 5-tuple carries many logical channels, multiplexed by `Channel`.
//   * Video/audio channels are unreliable / partially-reliable (drop-late,
//     FEC + selective NACK). Input/control/clipboard/file channels are fully
//     reliable, *independent* ordered streams (no head-of-line blocking of
//     input behind a file transfer).
//   * Every datagram begins with a fixed-size `FrameHeader`. The header rides
//     INSIDE the Noise ciphertext payload, not before it: the network sees only
//     opaque AEAD ciphertext (zero-knowledge relays, ADR §3.6 #2).
//   * Application-layer fragmentation/reassembly sits ABOVE Noise to respect the
//     65535-byte Noise transport-message cap (ADR §3.6 #5): a video keyframe or
//     file chunk larger than the cap is split into multiple frames sharing a
//     `seq`, ordered by fragment index, with kFlagFragment set and kFlagLastFrag
//     marking the final piece.

#include <cstdint>

#include "core/common/types.h"

namespace redesk::proto {

// ---------------------------------------------------------------------------
// Protocol version. Bumped on any incompatible framing change; negotiated in
// the control channel Hello exchange. Kept distinct from the product version.
// ---------------------------------------------------------------------------
inline constexpr uint16_t kProtocolVersion = 1;

// The Noise transport-message ceiling (ADR §3.6 #5). Anything larger MUST be
// fragmented above Noise. This is the *post*-encryption ceiling; payload budget
// per frame is this minus AEAD tag and FrameHeader overhead.
inline constexpr uint32_t kNoiseMessageMax = 65535;

// Usable application payload per frame after FrameHeader + Noise AEAD tag
// (ChaCha20-Poly1305 = 16-byte tag). Conservative; transports may compute a
// tighter value once the path MTU and Noise overhead are known.
inline constexpr uint32_t kAeadTagBytes = 16;

// ---------------------------------------------------------------------------
// Logical channels multiplexed over the single Noise/UDP 5-tuple (ADR §3.3).
// The integer values are part of the wire format — do NOT renumber.
// This enum is mirrored by core/transport's Channel; keep them in lockstep.
// ---------------------------------------------------------------------------
enum class Channel : uint8_t {
    kControl   = 0,  // Hello/version, capability negotiation, keepalive (reliable)
    kVideo     = 1,  // encoded video (unreliable, drop-late, FEC+NACK)
    kAudio     = 2,  // encoded audio (unreliable, drop-late)
    kInput     = 3,  // input events (reliable, immediate ARQ — never HoL-blocked)
    kClipboard = 4,  // clipboard sync incl. virtual-file streaming (reliable)
    kFile      = 5,  // file-transfer chunks (reliable; optional QUIC/BBR later)
    kStats     = 6,  // transport-wide-cc feedback, RTT, loss (low priority)
};

// Per-channel reliability policy. Mirrors ADR §3.3's reliable/unreliable split.
enum class Reliability : uint8_t {
    kUnreliable      = 0,  // fire-and-forget; drop-late
    kPartialReliable = 1,  // FEC + selective NACK before frame deadline
    kReliableOrdered = 2,  // independent ordered ARQ stream
};

// The canonical reliability for a channel. Transports must honor this so that,
// e.g., input is never delivered out of order and video is never retransmitted
// past its deadline.
constexpr Reliability reliabilityFor(Channel c) {
    switch (c) {
        case Channel::kControl:   return Reliability::kReliableOrdered;
        case Channel::kVideo:     return Reliability::kPartialReliable;
        case Channel::kAudio:     return Reliability::kUnreliable;
        case Channel::kInput:     return Reliability::kReliableOrdered;
        case Channel::kClipboard: return Reliability::kReliableOrdered;
        case Channel::kFile:      return Reliability::kReliableOrdered;
        case Channel::kStats:     return Reliability::kUnreliable;
    }
    return Reliability::kReliableOrdered;
}

// ---------------------------------------------------------------------------
// Frame flags — bitfield in FrameHeader.flags.
// ---------------------------------------------------------------------------
enum FrameFlags : uint16_t {
    kFlagNone      = 0,
    kFlagKeyframe  = 1u << 0,  // video frame is an IDR/keyframe
    kFlagFragment  = 1u << 1,  // this frame is one fragment of a larger message
    kFlagLastFrag  = 1u << 2,  // final fragment of a fragmented message
    kFlagFec       = 1u << 3,  // payload is FEC repair data, not source data
    kFlagRetransmit= 1u << 4,  // NACK-driven retransmission of a prior seq
    kFlagAck       = 1u << 5,  // payload carries ARQ ack/NACK info (reliable chans)
};

// ---------------------------------------------------------------------------
// Message type tags. The first byte(s) of a frame payload identify the concrete
// message for a given channel, so a single channel can carry several message
// kinds (e.g. control carries Hello, Capabilities, Keepalive). Values are wire
// format — append only, never renumber.
// ---------------------------------------------------------------------------
enum class MessageType : uint16_t {
    kUnknown = 0,

    // Control channel.
    kHello            = 100,  // protocol version + supported codec tiers
    kHelloAck         = 101,
    kCapabilities     = 102,  // negotiated capability grant set
    kKeepalive        = 103,
    kBye              = 104,
    kKeyframeRequest  = 105,  // receiver asks sender for an IDR (loss recovery)
    kRekey            = 106,  // request a Noise REKEY

    // Video / audio channels.
    kVideoFrame       = 200,
    kVideoFec         = 201,
    kAudioFrame       = 210,

    // Input channel.
    kInputEvent       = 300,

    // Clipboard channel.
    kClipboardText    = 400,
    kClipboardImage   = 401,
    kClipboardFiles   = 402,  // virtual-file offer; contents streamed on kFile

    // File channel.
    kFileOffer        = 500,
    kFileChunk        = 501,
    kFileAck          = 502,

    // Stats channel.
    kCcFeedback       = 600,  // transport-wide-cc / GCC feedback (ADR §3.3)
};

// ---------------------------------------------------------------------------
// Fixed-size frame header. Sits at the front of every (pre-encryption) payload.
//
// Layout is fixed and packed for a stable wire representation. Multi-byte
// fields are serialized little-endian by core/transport's codec helpers; this
// struct is the in-memory mirror. Size is intentionally small (12 bytes) to
// minimize per-datagram overhead.
//
//   channel : which logical stream this frame belongs to
//   type    : concrete message kind within the channel
//   flags   : FrameFlags bitfield
//   seq     : per-channel monotonic sequence; fragments of one message share it
//   frag_index / frag_count : reassembly index for fragmented messages
//   length  : byte length of the payload that follows this header
// ---------------------------------------------------------------------------
#pragma pack(push, 1)
struct FrameHeader {
    Channel    channel    = Channel::kControl;  // 1 byte
    uint8_t    reserved0  = 0;                   // 1 byte (alignment / future use)
    MessageType type      = MessageType::kUnknown; // 2 bytes
    uint16_t   flags      = kFlagNone;           // 2 bytes
    uint16_t   frag_index = 0;                   // 2 bytes
    uint16_t   frag_count = 1;                   // 2 bytes (1 == not fragmented)
    uint32_t   seq        = 0;                   // 4 bytes (logically wider; truncated for header)
    uint32_t   length     = 0;                   // 4 bytes — payload length in bytes
};
#pragma pack(pop)

static_assert(sizeof(FrameHeader) == 18,
              "FrameHeader wire size changed — bump kProtocolVersion and update peers");

constexpr bool hasFlag(const FrameHeader& h, FrameFlags f) {
    return (h.flags & static_cast<uint16_t>(f)) != 0;
}

// True when this frame is a standalone (non-fragmented) message.
constexpr bool isWholeMessage(const FrameHeader& h) {
    return h.frag_count <= 1 && !hasFlag(h, kFlagFragment);
}

// ---------------------------------------------------------------------------
// Fragmentation helper (ADR §3.6 #5). Given a total payload size and the path's
// usable payload budget, compute how many frames the message splits into. The
// budget is the Noise message ceiling minus header + AEAD tag overhead.
// ---------------------------------------------------------------------------
constexpr uint32_t usablePayloadBudget(uint32_t path_mtu_after_noise = kNoiseMessageMax) {
    const uint32_t overhead = sizeof(FrameHeader) + kAeadTagBytes;
    return path_mtu_after_noise > overhead ? path_mtu_after_noise - overhead : 0;
}

constexpr uint16_t fragmentCount(uint32_t total_bytes, uint32_t budget) {
    if (budget == 0) return 0;
    if (total_bytes == 0) return 1;
    const uint32_t n = (total_bytes + budget - 1) / budget;
    return static_cast<uint16_t>(n);
}

} // namespace redesk::proto
