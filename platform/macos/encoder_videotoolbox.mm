// platform/macos/encoder_videotoolbox.mm — macOS HW video encoder backend.
// ADR-001 §3.2 (Encode/Decode, macOS): VideoToolbox. HEVC is the practical
// default; runtime-PROBE for AV1 encode (newest Apple Silicon, e.g. M5 Pro/Max)
// rather than hard-coding "H.264/HEVC only."
//
// Compiled only on (APPLE AND REDESK_USE_REAL_BACKENDS).
//
// ADR corrections this backend MUST honor (see TODOs):
//   * Low latency: kVTCompressionPropertyKey_RealTime = true,
//     EnableLowLatencyRateControl, AllowFrameReordering = false (0 B-frames).
//   * Rate control: averageBitRate + DataRateLimits (ABR + HARD cap). Do NOT
//     set kVTCompressionPropertyKey_ConstantBitRate (incompatible with
//     low-latency RC for H.264).
//   * Loss recovery: LTR frames + ForceLTRRefresh (NO classic intra-refresh on
//     VideoToolbox). Prefer LTR over forced full IDRs.
//   * Input is the IOSurface-backed CVPixelBuffer from SCK (zero-copy).

#include "platform/platform_backends.h"

#if REDESK_USE_REAL_BACKENDS

#include "core/codec/encoder.h"  // redesk::codec::IVideoEncoder (core slice)

#import <VideoToolbox/VideoToolbox.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>

namespace redesk::platform {
namespace {

class VideoToolboxEncoder final : public redesk::codec::IVideoEncoder {
public:
    VideoToolboxEncoder() = default;
    ~VideoToolboxEncoder() override {
        // TODO(ADR §3.2): VTCompressionSessionInvalidate + CFRelease.
    }

    redesk::Status Configure(const redesk::codec::EncoderConfig& cfg) override {
        (void)cfg;
        // TODO(ADR §3.2): VTCompressionSessionCreate for the negotiated codec
        // (kCMVideoCodecType_HEVC default; H.264 floor; AV1 only if a one-frame
        // test encode succeeds). Set: RealTime=true, EnableLowLatencyRate-
        // Control=true, AllowFrameReordering=false, MaxKeyFrameInterval large
        // (rely on LTR not periodic IDR), AverageBitRate + DataRateLimits.
        // Enable LTR (kVTCompressionPropertyKey_EnableLTR if available) for the
        // loss-recovery path. Do NOT set ConstantBitRate.
        return redesk::Status::error(redesk::ErrorCode::Unsupported,
                                     "VideoToolbox Configure not implemented");
    }

    redesk::Result<redesk::EncodedPacket>
    Encode(const redesk::VideoFrame& frame, const redesk::codec::EncodeHints& hints) override {
        (void)frame; (void)hints;
        // TODO(ADR §3.2): VTCompressionSessionEncodeFrame with the CVPixelBuffer
        // from VideoFrame::native_handle. Apply hints: force-keyframe via the
        // kVTEncodeFrameOptionKey_ForceKeyFrame frame property; on loss, request
        // ForceLTRRefresh against the last acked LTR. Collect the
        // CMSampleBuffer in the output callback -> EncodedPacket.
        return redesk::Result<redesk::EncodedPacket>::fail(
            redesk::ErrorCode::Unsupported, "VideoToolbox encode not implemented");
    }

    redesk::Status RequestKeyframe() override {
        // TODO(ADR §3.2): prefer ForceLTRRefresh (acked LTR) over a forced IDR.
        return redesk::Status::error(redesk::ErrorCode::Unsupported,
                                     "RequestKeyframe not implemented");
    }
};

} // namespace

std::unique_ptr<redesk::codec::IVideoEncoder> CreateNativeVideoEncoder() {
    return std::make_unique<VideoToolboxEncoder>();
}

} // namespace redesk::platform

#endif // REDESK_USE_REAL_BACKENDS
