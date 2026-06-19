// Synthetic capturer stub (ADR-001 §3.1).
//
// Compiled by default (REDESK_USE_REAL_BACKENDS=OFF). It fabricates a single
// fake display and, on each manual pump(), emits a moving-gradient BGRA frame
// plus a slowly-circling cursor sample. Deterministic and timer-less so the
// transport/codec/session pipeline can be exercised in unit tests with no real
// hardware, no Qt, and no vendor SDKs.
//
// TODO(ADR §3.1): real backends live under platform/{windows,macos,linux} —
// DXGI Desktop Duplication (CopyResource->NV12->ReleaseFrame; handle
// DXGI_ERROR_ACCESS_LOST), ScreenCaptureKit (SCStream per SCDisplay, IOSurface),
// PipeWire portal (SPA_DATA_DmaBuf + restore_token). createCapturer() will
// dispatch to those when REDESK_USE_REAL_BACKENDS=ON.

#include "core/capture/capturer.h"

#include "core/common/logging.h"

namespace redesk::capture {
namespace {

constexpr uint32_t kStubWidth = 1280;
constexpr uint32_t kStubHeight = 720;

class StubCapturer final : public IStubCapturer {
public:
    Result<std::vector<DisplayInfo>> enumerateDisplays() override {
        DisplayInfo d;
        d.id = "stub-display-0";
        d.name = "REDesk Synthetic Display";
        d.size = {kStubWidth, kStubHeight};
        d.scale = 1.0f;
        d.isPrimary = true;
        return Result<std::vector<DisplayInfo>>::good({d});
    }

    void setFrameCallback(FrameCallback cb) override { frame_cb_ = std::move(cb); }
    void setCursorCallback(CursorCallback cb) override { cursor_cb_ = std::move(cb); }

    Status start(const CaptureConfig& config) override {
        if (capturing_) {
            return Status::error(ErrorCode::InvalidArgument, "already capturing");
        }
        config_ = config;
        // The stub honors the requested format only as a label; it always emits
        // CPU pixels in BGRA (no GPU surface available in a portable build).
        capturing_ = true;
        frame_index_ = 0;
        REDESK_LOG(Info, "capture")
            << "stub capture started on " << config.display_id;
        return Status::success();
    }

    void stop() override {
        if (capturing_) {
            REDESK_LOG(Info, "capture") << "stub capture stopped";
        }
        capturing_ = false;
    }

    bool isCapturing() const override { return capturing_; }

    void pump() override {
        if (!capturing_) {
            return;
        }
        deliverFrame();
        deliverCursor();
        ++frame_index_;
    }

private:
    void deliverFrame() {
        if (!frame_cb_) {
            return;
        }
        VideoFrame frame;
        frame.size = {kStubWidth, kStubHeight};
        frame.format = PixelFormat::BGRA8; // portable build => CPU pixels only
        frame.timestamp_us = frame_index_ * kFrameIntervalUs;
        frame.native_handle = nullptr;     // no GPU surface in the stub
        frame.is_idle = false;

        const size_t pixels = static_cast<size_t>(kStubWidth) * kStubHeight;
        frame.cpu_pixels.resize(pixels * 4);

        // Moving diagonal gradient: cheap, visually verifiable, frame-dependent
        // so encoders/decoders can confirm content actually changes per frame.
        const uint32_t shift = static_cast<uint32_t>(frame_index_ * 4);
        uint8_t* p = frame.cpu_pixels.data();
        for (uint32_t y = 0; y < kStubHeight; ++y) {
            for (uint32_t x = 0; x < kStubWidth; ++x) {
                const uint8_t b = static_cast<uint8_t>((x + shift) & 0xFF);
                const uint8_t g = static_cast<uint8_t>((y + shift) & 0xFF);
                const uint8_t r = static_cast<uint8_t>((x + y + shift) & 0xFF);
                *p++ = b;     // BGRA8
                *p++ = g;
                *p++ = r;
                *p++ = 0xFF;
            }
        }
        // Whole frame is "dirty" each tick (gradient scrolls everywhere).
        frame.dirty_rects.push_back(
            Rect{0, 0, static_cast<int32_t>(kStubWidth),
                 static_cast<int32_t>(kStubHeight)});
        frame_cb_(frame);
    }

    void deliverCursor() {
        if (!cursor_cb_ || !config_.capture_cursor) {
            return;
        }
        // 8x8 solid white square cursor, circling the center. Shape is sent only
        // on the first sample (subsequent samples carry shape_size {0,0} meaning
        // "shape unchanged"), mirroring real backends that diff cursor shapes.
        CursorSample c;
        const double angle = static_cast<double>(frame_index_) * 0.1;
        c.x = static_cast<int32_t>(kStubWidth / 2) +
              static_cast<int32_t>(200 * fastCos(angle));
        c.y = static_cast<int32_t>(kStubHeight / 2) +
              static_cast<int32_t>(200 * fastSin(angle));
        c.hotspot_x = 0;
        c.hotspot_y = 0;
        c.visible = true;
        c.timestamp_us = frame_index_ * kFrameIntervalUs;

        if (frame_index_ == 0) {
            c.shape_size = {8, 8};
            c.image.assign(static_cast<size_t>(8 * 8 * 4), 0xFF);
        }
        cursor_cb_(c);
    }

    // Tiny polynomial trig approximations so the stub doesn't pull in <cmath>
    // semantics that vary; accuracy is irrelevant, it just needs to circle.
    static double fastSin(double x) {
        // wrap to [-pi, pi]
        constexpr double kPi = 3.14159265358979323846;
        while (x > kPi) x -= 2 * kPi;
        while (x < -kPi) x += 2 * kPi;
        const double x2 = x * x;
        return x * (1.0 - x2 / 6.0 + x2 * x2 / 120.0);
    }
    static double fastCos(double x) { return fastSin(x + 1.57079632679); }

    static constexpr uint64_t kFrameIntervalUs = 16'666; // ~60 fps

    FrameCallback frame_cb_;
    CursorCallback cursor_cb_;
    CaptureConfig config_;
    bool capturing_ = false;
    uint64_t frame_index_ = 0;
};

} // namespace

std::unique_ptr<IStubCapturer> createStubCapturer() {
    return std::make_unique<StubCapturer>();
}

std::unique_ptr<ICapturer> createCapturer() {
#if defined(REDESK_USE_REAL_BACKENDS)
    // TODO(ADR §3.1): return platform/<os> real backend here.
    // For now even the "real" config falls back to the stub so the tree builds.
    return createStubCapturer();
#else
    return createStubCapturer();
#endif
}

} // namespace redesk::capture
