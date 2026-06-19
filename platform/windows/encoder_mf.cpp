// platform/windows/encoder_mf.cpp — Windows HW video encoder selector.
// ADR-001 §3.2 (Encode/Decode, Windows): a tiered, runtime-PROBED selector over
// the vendor SDKs called DIRECTLY (NVENC, AMF, Intel oneVPL/QSV) with Media
// Foundation as a last-resort fallback only — NOT routed through FFmpeg.
//
// ADR corrections this selector MUST honor (see TODOs):
//   * Every HW path is gated by a real one-frame test-encode at the target
//     profile/chroma — never by marketing tier names.
//   * Tier order: H.264 High 4:2:0 8-bit (always-present floor) -> HEVC
//     (preferred; 4:4:4 bound EXCLUSIVELY here for crisp text) -> AV1 (HW only,
//     opportunistic). H.264 4:4:4 is dropped entirely (no HW decode anywhere).
//   * Low-latency config: 0 B-frames, single ref, low-latency preset
//     (NVENC p1-p4 + tune ull), CBR/capped-VBR with tight VBV (~1 frame,
//     <=250 ms), slice-based, no look-ahead, no scene-cut detection.
//   * Loss recovery is PER-BACKEND: NVENC -> rolling intra-refresh +
//     reference-frame invalidation; QSV/AMF -> rolling intra-refresh where the
//     driver exposes it else periodic IDR with tight VBV. Prefer intra-refresh
//     / ref-invalidation over forced full IDRs.
//   * Input is the GPU-resident NV12/P010 texture from the capturer — no CPU
//     readback. Licensing: routing NVENC via FFmpeg needs --enable-nonfree
//     (un-redistributable); call the SDK directly.

#include "platform/platform_backends.h"

#if REDESK_USE_REAL_BACKENDS

#include "core/codec/encoder.h"  // redesk::codec::IVideoEncoder (core slice)

namespace redesk::platform {
namespace {

// Which concrete HW backend a probe selected.
enum class HwVendor { None, Nvenc, Amf, Qsv, MediaFoundation };

// Selector facade: picks the best vendor SDK that passes the one-frame test
// encode at the negotiated profile/chroma, then delegates the hot path to it.
class WindowsVideoEncoder final : public redesk::codec::IVideoEncoder {
public:
    WindowsVideoEncoder() = default;
    ~WindowsVideoEncoder() override = default;

    redesk::Status Configure(const redesk::codec::EncoderConfig& cfg) override {
        (void)cfg;
        // TODO(ADR §3.2): from the negotiated codec/profile/chroma, run a real
        // one-frame test encode in priority order:
        //   H.264 High floor:  NVENC -> QSV -> AMF -> Media Foundation
        //   HEVC tier:         NVENC -> QSV -> AMF (bind 4:4:4 here only)
        //   AV1 tier:          NVENC (Ada+) -> QSV (Arc) -> AMF (RDNA3+)
        // On the first success, latch HwVendor and apply low-latency RC:
        //   0 B-frames, single ref, ull/low-latency preset, CBR/capped-VBR,
        //   VBV ~1 frame (<=250 ms), slice-based, no look-ahead, no scene-cut.
        // Configure loss recovery for the chosen vendor (intra-refresh /
        // ref-invalidation). Bind the D3D11 device to the capturer's device so
        // the NV12 input texture imports zero-copy.
        return redesk::Status::error(redesk::ErrorCode::Unsupported,
                                     "Windows HW encoder Configure not implemented");
    }

    redesk::Result<redesk::EncodedPacket>
    Encode(const redesk::VideoFrame& frame, const redesk::codec::EncodeHints& hints) override {
        (void)frame; (void)hints;
        // TODO(ADR §3.2): submit the GPU-resident NV12/P010 texture to the
        // latched vendor encoder. Apply EncodeHints: ROI/QP from dirty_rects,
        // force-keyframe / ref-invalidation / LTR requests from the transport's
        // loss controller. Pull the bitstream and return EncodedPacket. Never
        // CPU-copy the input.
        return redesk::Result<redesk::EncodedPacket>::fail(
            redesk::ErrorCode::Unsupported, "Windows HW encode not implemented");
    }

    redesk::Status RequestKeyframe() override {
        // TODO(ADR §3.2): prefer rolling intra-refresh / reference-frame
        // invalidation over a forced full IDR (avoids the keyframe-size burst
        // the pacer/GCC governor is trying to prevent).
        return redesk::Status::error(redesk::ErrorCode::Unsupported,
                                     "RequestKeyframe not implemented");
    }

private:
    HwVendor vendor_ = HwVendor::None;
};

} // namespace

std::unique_ptr<redesk::codec::IVideoEncoder> CreateNativeVideoEncoder() {
    // TODO(ADR §3.2): the test-encode probing happens in Configure(); here we
    // just hand back the selector facade.
    return std::make_unique<WindowsVideoEncoder>();
}

} // namespace redesk::platform

#else // !REDESK_USE_REAL_BACKENDS

namespace redesk::platform {
std::unique_ptr<redesk::codec::IVideoEncoder> CreateNativeVideoEncoder() {
    return nullptr;
}
} // namespace redesk::platform

#endif // REDESK_USE_REAL_BACKENDS
