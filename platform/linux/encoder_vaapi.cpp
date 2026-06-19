// platform/linux/encoder_vaapi.cpp — Linux HW video encoder backend.
// ADR-001 §3.2 (Encode/Decode, Linux): VAAPI (Intel/AMD open path, default) and
// NVENC (NVIDIA Video Codec SDK, proprietary) — called DIRECTLY, not via FFmpeg.
//
// ADR corrections this backend MUST honor (see TODOs):
//   * Tier order: H.264 High 4:2:0 floor -> HEVC (4:4:4 bound here) -> AV1
//     (HW only, opportunistic). Every tier gated by a real one-frame test
//     encode at the target profile/chroma.
//   * Loss recovery: rolling intra-refresh where the driver exposes it, else
//     periodic IDR with tight VBV. Prefer intra-refresh over forced full IDRs.
//   * Input is the DMA-BUF imported VASurface (zero-copy) from the capturer;
//     handle the multi-GPU case (capture device != encode device) by routing
//     through the correct VADisplay / re-importing.
//   * Low-latency RC: 0 B-frames, single ref, CBR/capped-VBR, VBV ~1 frame
//     (<=250 ms), no look-ahead.

#include "platform/platform_backends.h"

#if REDESK_USE_REAL_BACKENDS

#include "core/codec/encoder.h"  // redesk::codec::IVideoEncoder (core slice)

#include <va/va.h>
#include <va/va_drm.h>

namespace redesk::platform {
namespace {

enum class HwVendor { None, Vaapi, Nvenc };

class LinuxVideoEncoder final : public redesk::codec::IVideoEncoder {
public:
    LinuxVideoEncoder() = default;
    ~LinuxVideoEncoder() override = default;

    redesk::Status Configure(const redesk::codec::EncoderConfig& cfg) override {
        (void)cfg;
        // TODO(ADR §3.2): open the encode VADisplay (vaGetDisplayDRM on the
        // render node). Query VAProfile/VAEntrypointEncSlice for the negotiated
        // codec; run a real one-frame test encode (H.264 High floor -> HEVC ->
        // AV1). If VAAPI lacks the profile and an NVIDIA card is present, latch
        // HwVendor::Nvenc via the Video Codec SDK. Apply low-latency RC
        // (VAEncMiscParameterRateControl CBR, VBV ~1 frame, 0 B-frames, single
        // ref); enable rolling intra-refresh if exposed.
        return redesk::Status::error(redesk::ErrorCode::Unsupported,
                                     "Linux HW encoder Configure not implemented");
    }

    redesk::Result<redesk::EncodedPacket>
    Encode(const redesk::VideoFrame& frame, const redesk::codec::EncodeHints& hints) override {
        (void)frame; (void)hints;
        // TODO(ADR §3.2): import the DMA-BUF from VideoFrame::native_handle as a
        // VASurface (vaCreateSurfaces + VASurfaceAttribExternalBuffers) on the
        // encode device; if capture/encode devices differ, re-import/copy across
        // the correct VADisplay. vaBeginPicture/Render/EndPicture; map the coded
        // buffer -> EncodedPacket. Apply ROI/QP hints from dirty_rects; honor
        // force-keyframe / intra-refresh requests.
        return redesk::Result<redesk::EncodedPacket>::fail(
            redesk::ErrorCode::Unsupported, "Linux HW encode not implemented");
    }

    redesk::Status RequestKeyframe() override {
        // TODO(ADR §3.2): trigger intra-refresh where exposed; else schedule an
        // IDR with the tight VBV to bound the burst.
        return redesk::Status::error(redesk::ErrorCode::Unsupported,
                                     "RequestKeyframe not implemented");
    }

private:
    HwVendor vendor_ = HwVendor::None;
    VADisplay va_display_ = nullptr;
};

} // namespace

std::unique_ptr<redesk::codec::IVideoEncoder> CreateNativeVideoEncoder() {
    return std::make_unique<LinuxVideoEncoder>();
}

} // namespace redesk::platform

#else // !REDESK_USE_REAL_BACKENDS

namespace redesk::platform {
std::unique_ptr<redesk::codec::IVideoEncoder> CreateNativeVideoEncoder() {
    return nullptr;
}
} // namespace redesk::platform

#endif // REDESK_USE_REAL_BACKENDS
