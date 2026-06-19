// platform/linux/capturer_pipewire.cpp — Linux screen capture backend.
// ADR-001 §3.1 (Screen Capture, Linux): Wayland PRIMARY via
// org.freedesktop.portal.ScreenCast (CreateSession -> SelectSources -> Start),
// negotiating SPA_DATA_DmaBuf and importing into VAAPI/EGL. X11 XShm is the
// CPU-bound COMPATIBILITY FLOOR only.
//
// ADR corrections this backend MUST honor (see TODOs):
//   * Persist restore_token (single-use, ROTATED each Start) to avoid re-prompts
//     — write ATOMICALLY (temp file + rename) immediately after each successful
//     Start, before doing anything else.
//   * DMA-BUF is BEST-EFFORT with a robust MemFd/SHM fallback — validate DRM
//     format modifiers against the encoder's importer; handle multi-GPU
//     (capture device vs encode device) mismatch.
//   * X11 XShm via XShmGetImage + XFixes cursor is a higher-latency tier that
//     VIOLATES the "no CPU readback" invariant — labeled + capped accordingly.
//   * Cursor on its own channel; DRM/KMS is privileged + cursor-incomplete —
//     prefer the portal.

#include "platform/platform_backends.h"

#if REDESK_USE_REAL_BACKENDS

#include "core/capture/capturer.h"  // redesk::capture::ICapturer (core slice)

#include <pipewire/pipewire.h>
#include <spa/param/video/format-utils.h>

namespace redesk::platform {
namespace {

// Capture tier chosen at Start() time by probing the session environment.
enum class Tier { None, WaylandPortalDmaBuf, WaylandPortalShm, X11XShm };

class PipeWirePortalCapturer final : public redesk::capture::ICapturer {
public:
    PipeWirePortalCapturer() = default;
    ~PipeWirePortalCapturer() override { Stop(); }

    redesk::Result<std::vector<redesk::capture::DisplayInfo>>
    EnumerateDisplays() override {
        // TODO(ADR §3.1): the portal does NOT enumerate displays up-front (the
        // user picks sources in the portal dialog). Report a single logical
        // "portal-selected" display pre-Start, or query outputs via wl_output
        // where a no-consent path is available. X11 path: XRandR.
        return redesk::Result<std::vector<redesk::capture::DisplayInfo>>::fail(
            redesk::ErrorCode::Unsupported, "PipeWire EnumerateDisplays not implemented");
    }

    redesk::Status Start(const redesk::capture::CaptureConfig& cfg) override {
        (void)cfg;
        // TODO(ADR §3.1): Wayland path —
        //   1. org.freedesktop.portal.ScreenCast: CreateSession, SelectSources
        //      (pass a persisted restore_token if we have one; persist_mode=2),
        //      Start; the reply carries the new restore_token + a PipeWire node.
        //   2. ATOMICALLY persist the new restore_token (temp + rename) NOW,
        //      before consuming frames — it is single-use and rotated each Start.
        //   3. pw_stream_connect on the node; negotiate SPA_DATA_DmaBuf; on
        //      success validate DRM format modifiers against the encoder's
        //      importer (multi-GPU: capture vs encode device may differ) ->
        //      Tier::WaylandPortalDmaBuf; else fall back to MemFd/SHM
        //      (Tier::WaylandPortalShm).
        //   X11 path (no Wayland session): open the display, XShmAttach a shared
        //   image -> Tier::X11XShm (CPU readback tier, latency-capped + flagged).
        //   Map a portal "consent denied" reply to ErrorCode::PermissionDenied.
        return redesk::Status::error(redesk::ErrorCode::Unsupported,
                                     "PipeWire/portal Start not implemented");
    }

    redesk::Status Stop() override {
        // TODO(ADR §3.1): pw_stream_disconnect, close the portal session,
        // detach XShm. Safe when not started.
        return redesk::Status::success();
    }

    redesk::Result<redesk::VideoFrame> AcquireFrame(uint32_t timeout_ms) override {
        (void)timeout_ms;
        // TODO(ADR §3.1): pop the latest pw_buffer. DmaBuf tier -> VideoFrame
        // with native_handle = a DMA-BUF fd-bundle descriptor (fds + strides +
        // modifier) for zero-copy VAAPI/EGL import; SHM tier -> copy into
        // cpu_pixels; X11 tier -> XShmGetImage into cpu_pixels (mark the frame
        // as the degraded CPU tier). Populate dirty_rects from SPA meta where
        // present; set is_idle when no damage.
        return redesk::Result<redesk::VideoFrame>::fail(
            redesk::ErrorCode::Unsupported, "PipeWire AcquireFrame not implemented");
    }

    redesk::Result<redesk::capture::CursorState> AcquireCursor() override {
        // TODO(ADR §3.1): portal cursor metadata (SPA_META_Cursor) when
        // embedded=false; X11 path uses XFixesGetCursorImage. Unify into the
        // shared cursor channel.
        return redesk::Result<redesk::capture::CursorState>::fail(
            redesk::ErrorCode::Unsupported, "PipeWire cursor not implemented");
    }

private:
    Tier tier_ = Tier::None;
    pw_loop* loop_ = nullptr;
    pw_stream* stream_ = nullptr;
};

} // namespace

std::unique_ptr<redesk::capture::ICapturer> CreateNativeCapturer() {
    // TODO(ADR §3.1): tier is decided in Start() by probing the live session;
    // here we just hand back the capturer that owns the ladder.
    return std::make_unique<PipeWirePortalCapturer>();
}

} // namespace redesk::platform

#else // !REDESK_USE_REAL_BACKENDS

namespace redesk::platform {
std::unique_ptr<redesk::capture::ICapturer> CreateNativeCapturer() {
    return nullptr;
}
} // namespace redesk::platform

#endif // REDESK_USE_REAL_BACKENDS
