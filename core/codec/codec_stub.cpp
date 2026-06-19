// Reversible pass-through codec stub + negotiation helper (ADR-001 §3.2).
//
// Compiled by default (REDESK_USE_REAL_BACKENDS=OFF). The "encoder" serializes
// the frame's CPU pixels behind a tiny self-describing header; the "decoder"
// parses it back into a VideoFrame. Lossless round-trip lets transport/session
// tests assert pixel-equality end-to-end without a real codec.
//
// negotiateCodec() is real logic (not stubbed) — it's pure capability math the
// production path also uses. Only the encode/decode byte-shovelling is fake.
//
// TODO(ADR §3.2): real HW encoders/decoders (NVENC/AMF/QSV/MF, VideoToolbox,
// VAAPI) live under platform/<os>; call vendor SDKs directly (NOT via FFmpeg for
// encode — --enable-nonfree taint). Gate each tier on a real test-encode probe.

#include "core/codec/codec.h"

#include <cstring>

#include "core/common/logging.h"

namespace redesk::codec {

const char* toString(Codec c) noexcept {
    switch (c) {
        case Codec::H264: return "H264";
        case Codec::HEVC: return "HEVC";
        case Codec::AV1:  return "AV1";
    }
    return "?";
}

NegotiatedCodec negotiateCodec(const CodecCapabilities& local,
                               const CodecCapabilities& remote,
                               bool localIsSender) {
    // For a given direction, the sender must ENCODE and the receiver must DECODE.
    const CodecCapabilities& enc = localIsSender ? local : remote;
    const CodecCapabilities& dec = localIsSender ? remote : local;

    NegotiatedCodec out;

    // Tier 3: AV1 (HW only) — opportunistic top tier.
    if (enc.av1_encode && dec.av1_decode) {
        out = {true, Codec::AV1, ChromaFormat::Yuv420};
        return out;
    }
    // Tier 2: HEVC. Prefer 4:4:4 (crisp text) when both ends support it.
    if (enc.hevc_encode && dec.hevc_decode) {
        const bool fourfourfour = enc.hevc_444_encode && dec.hevc_444_decode;
        out = {true, Codec::HEVC,
               fourfourfour ? ChromaFormat::Yuv444 : ChromaFormat::Yuv420};
        return out;
    }
    // Tier 1: H.264 floor. §3.2 forbids shipping without this on both ends.
    if (enc.h264_encode && dec.h264_decode) {
        out = {true, Codec::H264, ChromaFormat::Yuv420};
        return out;
    }
    // No common codec — connection cannot carry video.
    out.ok = false;
    return out;
}

CodecCapabilities localCodecCapabilities() {
    // The stub claims everything so negotiation tests can reach every tier. Real
    // builds populate this from runtime probes (§3.2).
    CodecCapabilities caps;
    caps.h264_encode = caps.h264_decode = true;
    caps.hevc_encode = caps.hevc_decode = true;
    caps.hevc_444_encode = caps.hevc_444_decode = true;
    caps.av1_encode = caps.av1_decode = true;
    return caps;
}

namespace {

// Wire layout of a stub packet: a fixed magic, then the original frame metadata,
// then the raw BGRA bytes. Keeps the round-trip exact so tests can assert equality.
constexpr uint32_t kStubMagic = 0x52454442; // "REDB"

struct StubHeader {
    uint32_t magic;
    uint32_t width;
    uint32_t height;
    uint32_t format;     // PixelFormat
    uint64_t timestamp_us;
    uint32_t keyframe;
    uint32_t payload_len;
};

void appendPod(std::vector<uint8_t>& out, const void* data, size_t len) {
    const auto* p = static_cast<const uint8_t*>(data);
    out.insert(out.end(), p, p + len);
}

class StubEncoder final : public IVideoEncoder {
public:
    explicit StubEncoder(Codec codec) : codec_(codec) {}

    Status configure(const EncoderConfig& config) override {
        config_ = config;
        codec_ = config.codec;
        REDESK_LOG(Info, "codec")
            << "stub encoder configured: " << toString(codec_) << " "
            << config.resolution.width << "x" << config.resolution.height << " @"
            << config.target_bitrate_kbps << "kbps";
        return Status::success();
    }

    void setPacketCallback(EncodedPacketCallback cb) override {
        packet_cb_ = std::move(cb);
    }

    Status encode(const VideoFrame& frame) override {
        if (!packet_cb_) {
            return Status::error(ErrorCode::InvalidArgument,
                                 "no packet callback installed");
        }
        // The stub can only round-trip CPU-resident frames (no GPU surface in a
        // portable build). Real encoders consume frame.native_handle directly.
        if (frame.cpu_pixels.empty() && frame.native_handle != nullptr) {
            return Status::error(ErrorCode::Unsupported,
                                 "stub encoder requires CPU pixels");
        }

        const bool keyframe = force_keyframe_ || first_frame_;
        StubHeader hdr{};
        hdr.magic = kStubMagic;
        hdr.width = frame.size.width;
        hdr.height = frame.size.height;
        hdr.format = static_cast<uint32_t>(frame.format);
        hdr.timestamp_us = frame.timestamp_us;
        hdr.keyframe = keyframe ? 1u : 0u;
        hdr.payload_len = static_cast<uint32_t>(frame.cpu_pixels.size());

        EncodedPacket pkt;
        pkt.data.reserve(sizeof(StubHeader) + frame.cpu_pixels.size());
        appendPod(pkt.data, &hdr, sizeof(hdr));
        appendPod(pkt.data, frame.cpu_pixels.data(), frame.cpu_pixels.size());
        pkt.timestamp_us = frame.timestamp_us;
        pkt.keyframe = keyframe;

        force_keyframe_ = false;
        first_frame_ = false;
        packet_cb_(pkt);
        return Status::success();
    }

    void requestKeyframe() override { force_keyframe_ = true; }

    void forceIntraRefresh() override {
        // Stub has no intra-refresh; fall back to a keyframe (real backends use
        // rolling intra-refresh / LTR per §3.2).
        force_keyframe_ = true;
    }

    Codec codec() const override { return codec_; }

private:
    Codec codec_;
    EncoderConfig config_;
    EncodedPacketCallback packet_cb_;
    bool force_keyframe_ = false;
    bool first_frame_ = true;
};

class StubDecoder final : public IVideoDecoder {
public:
    explicit StubDecoder(Codec codec) : codec_(codec) {}

    Status configure(Codec codec) override {
        codec_ = codec;
        return Status::success();
    }

    void setFrameCallback(DecodedFrameCallback cb) override {
        frame_cb_ = std::move(cb);
    }

    Status decode(const EncodedPacket& packet) override {
        if (!frame_cb_) {
            return Status::error(ErrorCode::InvalidArgument,
                                 "no frame callback installed");
        }
        if (packet.data.size() < sizeof(StubHeader)) {
            return Status::error(ErrorCode::InvalidArgument, "short packet");
        }
        StubHeader hdr{};
        std::memcpy(&hdr, packet.data.data(), sizeof(hdr));
        if (hdr.magic != kStubMagic) {
            return Status::error(ErrorCode::InvalidArgument, "bad stub magic");
        }
        const size_t expected = sizeof(StubHeader) + hdr.payload_len;
        if (packet.data.size() < expected) {
            return Status::error(ErrorCode::InvalidArgument,
                                 "truncated stub payload");
        }

        VideoFrame frame;
        frame.size = {hdr.width, hdr.height};
        frame.format = static_cast<PixelFormat>(hdr.format);
        frame.timestamp_us = hdr.timestamp_us;
        frame.cpu_pixels.assign(packet.data.begin() + sizeof(StubHeader),
                                packet.data.begin() + expected);
        frame_cb_(frame);
        return Status::success();
    }

    Codec codec() const override { return codec_; }

private:
    Codec codec_;
    DecodedFrameCallback frame_cb_;
};

} // namespace

std::unique_ptr<IVideoEncoder> createVideoEncoder(Codec codec) {
#if defined(REDESK_USE_REAL_BACKENDS)
    // TODO(ADR §3.2): dispatch to platform/<os> HW encoder; fall back to stub.
    return std::make_unique<StubEncoder>(codec);
#else
    return std::make_unique<StubEncoder>(codec);
#endif
}

std::unique_ptr<IVideoDecoder> createVideoDecoder(Codec codec) {
#if defined(REDESK_USE_REAL_BACKENDS)
    // TODO(ADR §3.2): dispatch to platform/<os> HW decoder (D3D11VA/VT/VAAPI).
    return std::make_unique<StubDecoder>(codec);
#else
    return std::make_unique<StubDecoder>(codec);
#endif
}

} // namespace redesk::codec
