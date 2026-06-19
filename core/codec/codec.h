#pragma once

// Encode/decode abstraction + codec negotiation (ADR-001 §3.2).
//
// Codec-agnostic IVideoEncoder / IVideoDecoder with tiered, bidirectionally
// negotiated selection:
//   1. H.264 High 4:2:0 8-bit  — the always-present HW floor.
//   2. HEVC                    — preferred upgrade; the ONLY tier carrying 4:4:4.
//   3. AV1 (HW only)           — opportunistic, both ends must pass a runtime probe.
//
// §3.2 mandates real test-encode/test-decode probes before trusting any HW tier;
// negotiateCodec() here only intersects *declared* capabilities — the probe lives
// in platform/. Low-latency config (zero B-frames, single ref, tight VBV) is
// expressed in EncoderConfig; per-backend loss recovery (NVENC intra-refresh,
// VideoToolbox LTR) is surfaced via requestKeyframe()/forceIntraRefresh().
//
// The default STUB (REDESK_USE_REAL_BACKENDS=OFF) is a reversible pass-through:
// the encoder wraps the frame's CPU pixels in an EncodedPacket with a small
// header, and the decoder reconstructs the original VideoFrame. No real codec.

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "core/common/types.h"

namespace redesk::codec {

enum class Codec {
    H264,   // High 4:2:0 8-bit floor (§3.2.1)
    HEVC,   // preferred; 4:4:4 bound here (§3.2.2)
    AV1,    // opportunistic HW-only (§3.2.3)
};

const char* toString(Codec c) noexcept;

// Chroma subsampling. 4:4:4 is HEVC-only per §3.2 ("H.264 4:4:4 has zero HW
// decode on any vendor and is dropped entirely").
enum class ChromaFormat {
    Yuv420,
    Yuv444,
};

// Rate-control mode. §3.2: CBR/capped-VBR with tight VBV; never set VideoToolbox
// ConstantBitRate alongside low-latency RC for H.264.
enum class RateControl {
    Cbr,
    CappedVbr,
};

struct EncoderConfig {
    Codec codec = Codec::H264;
    Size resolution;
    ChromaFormat chroma = ChromaFormat::Yuv420;
    uint32_t target_bitrate_kbps = 8'000;
    uint32_t max_bitrate_kbps = 0;      // 0 => derive (~1.5x target) for capped-VBR
    uint32_t max_fps = 60;
    RateControl rate_control = RateControl::CappedVbr;
    bool low_latency = true;            // zero B-frames, single ref, no lookahead
    uint32_t vbv_max_ms = 250;          // tight VBV (§3.2): <= ~250 ms
    PixelFormat input_format = PixelFormat::NV12; // HW-encoder-friendly input
};

// Per-codec runtime capability set, exchanged during session setup and fed to
// negotiateCodec(). In production each `true` here must be backed by a real
// test-encode/test-decode probe (§3.2) before being advertised.
struct CodecCapabilities {
    bool h264_encode = false;
    bool h264_decode = false;
    bool hevc_encode = false;
    bool hevc_decode = false;
    bool hevc_444_encode = false;       // 4:4:4 crisp-text path (HEVC only)
    bool hevc_444_decode = false;
    bool av1_encode = false;
    bool av1_decode = false;
};

// Result of negotiation: the codec + chroma both ends agreed on. `ok` is false
// only if the H.264 floor itself is missing (which §3.2 forbids shipping).
struct NegotiatedCodec {
    bool ok = false;
    Codec codec = Codec::H264;
    ChromaFormat chroma = ChromaFormat::Yuv420;
};

// Intersect local + remote capabilities and pick the highest tier both ends can
// SEND/RECEIVE for this direction. `localIsSender` decides which side encodes.
// Prefers AV1 > HEVC(4:4:4 if both support) > HEVC(4:2:0) > H.264 floor.
NegotiatedCodec negotiateCodec(const CodecCapabilities& local,
                               const CodecCapabilities& remote,
                               bool localIsSender);

using EncodedPacketCallback = std::function<void(const EncodedPacket&)>;

class IVideoEncoder {
public:
    virtual ~IVideoEncoder() = default;

    // Apply/replace configuration. May be called at runtime to retarget bitrate
    // (GCC drives this per §3.3); resolution/codec changes may force a re-init.
    virtual Status configure(const EncoderConfig& config) = 0;

    // Install the bitstream sink. Encoders may emit zero or one packet per
    // encode() call (HW encoders are 1:1 in low-latency mode; no B-frame delay).
    virtual void setPacketCallback(EncodedPacketCallback cb) = 0;

    // Submit one frame for encoding. Output is delivered via the packet callback.
    virtual Status encode(const VideoFrame& frame) = 0;

    // Request an IDR/keyframe on the next encode (recovery of last resort).
    virtual void requestKeyframe() = 0;

    // Prefer rolling intra-refresh / LTR / ref-invalidation over a full IDR for
    // loss recovery (§3.2). Backends without intra-refresh fall back to keyframe.
    virtual void forceIntraRefresh() = 0;

    virtual Codec codec() const = 0;
};

using DecodedFrameCallback = std::function<void(const VideoFrame&)>;

class IVideoDecoder {
public:
    virtual ~IVideoDecoder() = default;

    virtual Status configure(Codec codec) = 0;
    virtual void setFrameCallback(DecodedFrameCallback cb) = 0;

    // Submit one encoded packet. Output is delivered via the frame callback. May
    // produce zero frames (e.g. parameter-set-only packets) or one frame.
    virtual Status decode(const EncodedPacket& packet) = 0;

    virtual Codec codec() const = 0;
};

// Factories: real HW backend when REDESK_USE_REAL_BACKENDS=ON, else stub.
std::unique_ptr<IVideoEncoder> createVideoEncoder(Codec codec);
std::unique_ptr<IVideoDecoder> createVideoDecoder(Codec codec);

// Capabilities the local build can actually advertise. Stub reports a full set
// (everything "supported") so negotiation tests can exercise every tier.
CodecCapabilities localCodecCapabilities();

} // namespace redesk::codec
