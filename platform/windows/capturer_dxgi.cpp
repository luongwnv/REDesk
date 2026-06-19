// platform/windows/capturer_dxgi.cpp — Windows screen capture backend.
// ADR-001 §3.1 (Screen Capture, Windows): DXGI Desktop Duplication (DDA) as the
// primary path with a Windows.Graphics.Capture (WGC) fallback.
//
// Corrections from the ADR that this backend MUST implement (see TODOs):
//   * Lifecycle: CopyResource/color-convert into an app-owned NV12 texture and
//     ReleaseFrame IMMEDIATELY — never hold the duplication surface across the
//     encode (the surface is only valid until ReleaseFrame).
//   * DXGI_ERROR_ACCESS_LOST (mode/desktop/Secure-Desktop/fullscreen switch):
//     tear down and re-duplicate; surface as redesk::ErrorCode::AccessLost.
//   * BGRA->NV12 GPU convert (compute/pixel shader), no CPU readback.
//   * WGC build gates via ApiInformation: IsBorderRequired=false only on Win11
//     build 20348+, IsCursorCaptureEnabled only on Win10 2004+.
//   * Cursor goes on its OWN channel (GetFramePointerShape), never composited.
//   * Protected/DRM content returns black on both DDA and WGC — expected.

#include "platform/platform_backends.h"

#if REDESK_USE_REAL_BACKENDS

#include "core/capture/capturer.h"  // redesk::capture::ICapturer (core slice)

// Windows / DXGI headers are intentionally only included on the real path so the
// default build never needs the Windows SDK.
#include <d3d11.h>
#include <dxgi1_5.h>     // IDXGIOutputDuplication, IDXGIOutput5::DuplicateOutput1
#include <wrl/client.h>  // Microsoft::WRL::ComPtr

namespace redesk::platform {
namespace {

using Microsoft::WRL::ComPtr;

// DXGI Desktop Duplication capturer (one IDXGIOutputDuplication per output).
// Falls back to WGC for single-window capture, RDP/virtual displays, locked
// sessions, and under GPU saturation (AcquireNextFrame stalls).
class DxgiCapturer final : public redesk::capture::ICapturer {
public:
    DxgiCapturer() = default;
    ~DxgiCapturer() override { Stop(); }

    redesk::Result<std::vector<redesk::capture::DisplayInfo>>
    EnumerateDisplays() override {
        // TODO(ADR §3.1): IDXGIFactory::EnumAdapters -> EnumOutputs; report
        // bounds, DPI scale, HDR/FP16 capability (drives DuplicateOutput1).
        return redesk::Result<std::vector<redesk::capture::DisplayInfo>>::fail(
            redesk::ErrorCode::Unsupported, "DXGI EnumerateDisplays not implemented");
    }

    redesk::Status Start(const redesk::capture::CaptureConfig& cfg) override {
        // TODO(ADR §3.1): create D3D11 device on the capture adapter; for each
        // target output call IDXGIOutput5::DuplicateOutput1 (HDR/FP16 formats)
        // or DuplicateOutput. Allocate an app-owned NV12 (or P010) texture pool
        // sized to the output, plus the BGRA->NV12 GPU-convert shader.
        // Raise the capture-thread priority to survive fullscreen-game GPU
        // saturation before considering the WGC fallback.
        (void)cfg;
        return redesk::Status::error(redesk::ErrorCode::Unsupported,
                                     "DXGI Start not implemented");
    }

    redesk::Status Stop() override {
        // TODO(ADR §3.1): ReleaseFrame if held, drop IDXGIOutputDuplication,
        // release the device/convert resources. Safe to call when not started.
        return redesk::Status::success();
    }

    // Hot path. Pull one frame, GPU color-convert, release the duplication
    // surface immediately, hand back an app-owned GPU texture.
    redesk::Result<redesk::VideoFrame> AcquireFrame(uint32_t timeout_ms) override {
        (void)timeout_ms;
        // TODO(ADR §3.1, lifecycle correction):
        //   1. AcquireNextFrame -> ID3D11Texture2D + DXGI_OUTDUPL_FRAME_INFO.
        //   2. If LastPresentTime == 0 and no dirty/move rects -> idle frame:
        //      mark VideoFrame::is_idle, ReleaseFrame, return (idle-skip).
        //   3. Read dirty/move rects (GetFrameDirtyRects/GetFrameMoveRects) into
        //      VideoFrame::dirty_rects for ROI/QP hints + bandwidth (NOT partial
        //      HW encode — mainstream HW encoders encode full frames).
        //   4. CopyResource/run BGRA->NV12 convert into an app-owned texture.
        //   5. ReleaseFrame IMMEDIATELY (surface invalid afterwards).
        //   6. Return VideoFrame{ native_handle = app-owned ID3D11Texture2D* }.
        //   On DXGI_ERROR_ACCESS_LOST -> ErrorCode::AccessLost so the session
        //   re-duplicates; on DXGI_ERROR_WAIT_TIMEOUT -> ErrorCode::Again.
        return redesk::Result<redesk::VideoFrame>::fail(
            redesk::ErrorCode::Unsupported, "DXGI AcquireFrame not implemented");
    }

    // Separate cursor channel (ADR §3.1 cross-cutting: unify cursor models).
    redesk::Result<redesk::capture::CursorState> AcquireCursor() override {
        // TODO(ADR §3.1): IDXGIOutputDuplication::GetFramePointerShape +
        // DXGI_OUTDUPL_POINTER_POSITION; normalize into the shared cursor model.
        return redesk::Result<redesk::capture::CursorState>::fail(
            redesk::ErrorCode::Unsupported, "DXGI cursor not implemented");
    }

private:
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGIOutputDuplication> dupl_;
    // WGC fallback (Windows.Graphics.Capture) state lives behind the same
    // interface; selected at runtime when DDA is unavailable / stalling.
};

} // namespace

std::unique_ptr<redesk::capture::ICapturer> CreateNativeCapturer() {
    // TODO(ADR §3.1): runtime-select DDA vs WGC. Probe whether DDA can
    // duplicate the target output (it cannot on some RDP/virtual displays); if
    // not, return the WGC capturer instead. Both deliver D3D11 textures.
    return std::make_unique<DxgiCapturer>();
}

} // namespace redesk::platform

#else // !REDESK_USE_REAL_BACKENDS

// Stub build: no Windows SDK, no capture. The core stub capturer is used.
namespace redesk::platform {
std::unique_ptr<redesk::capture::ICapturer> CreateNativeCapturer() {
    return nullptr;
}
} // namespace redesk::platform

#endif // REDESK_USE_REAL_BACKENDS
