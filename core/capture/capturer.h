#pragma once

// Screen-capture abstraction (ADR-001 §3.1).
//
// One ICapturer interface, three native backends (DXGI DDA / ScreenCaptureKit /
// PipeWire-portal) selected by platform/. The hot path keeps frames GPU-resident
// (VideoFrame::native_handle); only the CPU/stub tiers fill VideoFrame::cpu_pixels.
//
// Two correctness invariants from §3.1 that backends MUST honor and that this
// interface is shaped around:
//   * Frame lifetime: a backend MUST copy/color-convert into an app-owned NV12
//     texture and release the OS duplication surface BEFORE the frame callback
//     returns. Callbacks therefore receive an owned/stable VideoFrame, never a
//     borrowed duplication surface held across encode.
//   * Cursor is a SEPARATE channel (DDA GetFramePointerShape / SCK / XFixes are
//     unified here) so the renderer can composite a low-latency local cursor.
//
// The default STUB (compiled when REDESK_USE_REAL_BACKENDS=OFF) is deterministic
// and timer-less: it synthesizes a moving-gradient BGRA frame each time pump()
// is called, so tests drive the pipeline without real hardware or threads.

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/common/types.h"

namespace redesk::capture {

// A capturable output. `id` is a stable backend handle (DXGI output index /
// SCDisplay displayID / PipeWire node id) the caller passes back to start().
struct DisplayInfo {
    std::string id;             // opaque, backend-stable
    std::string name;           // human label (e.g. "DELL U2720Q")
    Size size;                  // pixel dimensions (physical, post-scale)
    float scale = 1.0f;         // DPI/Retina backing scale (2.0 == Retina)
    bool isPrimary = false;
};

// Unified cursor sample (the §3.1 separate cursor channel). Shape pixels are
// BGRA premultiplied; an empty `image` with `visible=false` means "hide".
struct CursorSample {
    int32_t x = 0;              // hotspot position in display coordinates
    int32_t y = 0;
    int32_t hotspot_x = 0;      // hotspot offset within the shape
    int32_t hotspot_y = 0;
    Size shape_size;            // cursor bitmap dimensions (0 if shape unchanged)
    std::vector<uint8_t> image; // BGRA premultiplied; empty if shape unchanged
    bool visible = true;
    uint64_t timestamp_us = 0;
};

// Capture tuning. Backends clamp to what the OS/HW supports.
struct CaptureConfig {
    std::string display_id;             // which display (from enumerateDisplays)
    uint32_t target_fps = 60;           // minimumFrameInterval = 1/target_fps
    bool capture_cursor = true;         // route cursor onto the cursor channel
    bool prefer_hdr = false;            // P010/FP16 path where available (§3.1)
    PixelFormat preferred_format = PixelFormat::NV12; // encoder-friendly default
};

using FrameCallback = std::function<void(const VideoFrame&)>;
using CursorCallback = std::function<void(const CursorSample&)>;

// Abstract capturer. Lifetime: construct -> setCallbacks -> start -> (frames) ->
// stop. Not required to be thread-safe across start/stop; the frame callback is
// invoked on the capture thread (real backends) or the pump() caller (stub).
class ICapturer {
public:
    virtual ~ICapturer() = default;

    // Enumerate currently attached displays. Safe to call before start().
    virtual Result<std::vector<DisplayInfo>> enumerateDisplays() = 0;

    // Install delivery callbacks. Must be called before start(). The cursor
    // callback may be left null if the caller doesn't render a remote cursor.
    virtual void setFrameCallback(FrameCallback cb) = 0;
    virtual void setCursorCallback(CursorCallback cb) = 0;

    // Begin capturing `config.display_id`. Returns Unsupported/PermissionDenied
    // (macOS TCC, Wayland portal) per ErrorCode; AccessLost is reported later via
    // a zero-size frame + status, since DXGI ACCESS_LOST is recoverable (§3.1).
    virtual Status start(const CaptureConfig& config) = 0;

    // Stop capture and release OS resources. Idempotent.
    virtual void stop() = 0;

    virtual bool isCapturing() const = 0;
};

// Factory. Returns the real backend for REDESK_PLATFORM when
// REDESK_USE_REAL_BACKENDS=ON, else the synthetic stub. Never null.
std::unique_ptr<ICapturer> createCapturer();

// Test-only handle: the stub capturer additionally exposes a manual frame pump
// (timer-less). The factory above hides it behind ICapturer; tests that need
// deterministic stepping use createStubCapturer() and call pump().
class IStubCapturer : public ICapturer {
public:
    // Synthesize and deliver exactly one frame (moving gradient) + one cursor
    // sample if a cursor callback is installed. No-op if not started.
    virtual void pump() = 0;
};

std::unique_ptr<IStubCapturer> createStubCapturer();

} // namespace redesk::capture
