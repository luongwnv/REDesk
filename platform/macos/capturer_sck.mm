// platform/macos/capturer_sck.mm — macOS screen capture backend.
// ADR-001 §3.1 (Screen Capture, macOS): ScreenCaptureKit ONLY. One SCStream per
// SCDisplay, IOSurface-backed CVPixelBuffer feeds VideoToolbox zero-copy.
//
// Compiled only on (APPLE AND REDESK_USE_REAL_BACKENDS); the guard below is
// defense-in-depth so this file is harmless if ever compiled elsewhere.
//
// ADR corrections this backend MUST honor (see TODOs):
//   * Configure Retina scale, color space (P3/HDR), minimumFrameInterval=1/60,
//     cursor mode; deliver IOSurface-backed CVPixelBuffer (no CPU readback).
//   * TCC (high confidence): Screen Recording must be RE-CONFIRMED roughly
//     monthly and after every reboot on macOS 15/26. Mitigation for unattended
//     use is MDM-delivered PPPC/TCC profiles + a signed, NOTARIZED .app bundle
//     — a bare executable will NOT appear in the Screen Recording list on
//     macOS 26.1+. Budget re-grant UX.
//   * Pre-login (high confidence): SCK FAILS at the macOS Login Window from a
//     system daemon (null buffer, no graphical context). Scope to POST-LOGIN
//     capture; login-window control is a research risk, not a feature.

#include "platform/platform_backends.h"

#if REDESK_USE_REAL_BACKENDS

#include "core/capture/capturer.h"  // redesk::capture::ICapturer (core slice)

#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <CoreVideo/CoreVideo.h>
#import <IOSurface/IOSurface.h>

namespace redesk::platform {
namespace {

class SckCapturer final : public redesk::capture::ICapturer {
public:
    SckCapturer() = default;
    ~SckCapturer() override { Stop(); }

    redesk::Result<std::vector<redesk::capture::DisplayInfo>>
    EnumerateDisplays() override {
        // TODO(ADR §3.1): SCShareableContent.getShareableContentWithCompletion-
        // Handler -> map SCDisplay[] to DisplayInfo (bounds, scale, HDR).
        return redesk::Result<std::vector<redesk::capture::DisplayInfo>>::fail(
            redesk::ErrorCode::Unsupported, "SCK EnumerateDisplays not implemented");
    }

    redesk::Status Start(const redesk::capture::CaptureConfig& cfg) override {
        (void)cfg;
        // TODO(ADR §3.1): build SCContentFilter (per SCDisplay), an
        // SCStreamConfiguration (pixelFormat = kCVPixelFormatType_420YpCbCr8Bi-
        // PlanarFullRange or P010 for HDR, colorSpaceName P3, width/height at
        // Retina scale, minimumFrameInterval = CMTimeMake(1,60), showsCursor per
        // policy). Create one SCStream per display, add a stream output on a
        // dedicated dispatch queue, startCaptureWithCompletionHandler.
        // Gate first on CGPreflight / SCShareableContent error == TCC denied ->
        // ErrorCode::PermissionDenied so the UI can drive re-grant onboarding.
        return redesk::Status::error(redesk::ErrorCode::Unsupported,
                                     "SCK Start not implemented");
    }

    redesk::Status Stop() override {
        // TODO(ADR §3.1): stopCaptureWithCompletionHandler on each SCStream,
        // release filters/configs. Safe when not started.
        return redesk::Status::success();
    }

    redesk::Result<redesk::VideoFrame> AcquireFrame(uint32_t timeout_ms) override {
        (void)timeout_ms;
        // TODO(ADR §3.1): pop the latest CMSampleBuffer delivered by the stream
        // output; pull its IOSurface-backed CVPixelBuffer; populate VideoFrame
        // with native_handle = CVPixelBufferRef/IOSurfaceRef (retained for the
        // encoder), dirty_rects from SCStreamFrameInfoDirtyRects attachment,
        // and is_idle when SCFrameStatus == .idle. Never CPU-copy.
        return redesk::Result<redesk::VideoFrame>::fail(
            redesk::ErrorCode::Unsupported, "SCK AcquireFrame not implemented");
    }

    redesk::Result<redesk::capture::CursorState> AcquireCursor() override {
        // TODO(ADR §3.1): SCK can composite or separate the cursor; when
        // showsCursor=false, source cursor image/hotspot/position from the
        // NSCursor / SCStreamFrameInfo and emit on the unified cursor channel.
        return redesk::Result<redesk::capture::CursorState>::fail(
            redesk::ErrorCode::Unsupported, "SCK cursor not implemented");
    }
};

} // namespace

std::unique_ptr<redesk::capture::ICapturer> CreateNativeCapturer() {
    return std::make_unique<SckCapturer>();
}

} // namespace redesk::platform

#endif // REDESK_USE_REAL_BACKENDS
