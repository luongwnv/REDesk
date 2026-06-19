#pragma once

// Shared value types used across every REDesk core interface. Header-only,
// dependency-free (std only) so it can be included by core/, platform/,
// service/, ui/, and tests/ without pulling in Qt/FFmpeg/SDKs.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace redesk {

// ---------------------------------------------------------------------------
// Error handling: a tiny Result type. Engine code avoids exceptions on the hot
// path; backends return Status and richer Result<T> for fallible factories.
// ---------------------------------------------------------------------------
enum class ErrorCode {
    Ok = 0,
    Unsupported,        // backend/codec/feature not available on this host
    PermissionDenied,   // e.g. macOS TCC / Wayland portal consent missing
    NotFound,
    InvalidArgument,
    Timeout,
    ConnectionLost,
    AccessLost,         // DXGI_ERROR_ACCESS_LOST and analogues — recoverable
    Again,              // transient; retry
    Internal,
};

struct Status {
    ErrorCode code = ErrorCode::Ok;
    std::string message;

    bool ok() const { return code == ErrorCode::Ok; }
    explicit operator bool() const { return ok(); }

    static Status success() { return {}; }
    static Status error(ErrorCode c, std::string msg) { return {c, std::move(msg)}; }
};

template <typename T>
struct Result {
    Status status;
    T value{};

    bool ok() const { return status.ok(); }
    explicit operator bool() const { return ok(); }

    static Result good(T v) { return {Status::success(), std::move(v)}; }
    static Result fail(ErrorCode c, std::string msg) {
        return {Status::error(c, std::move(msg)), T{}};
    }
};

// ---------------------------------------------------------------------------
// Geometry / pixel description.
// ---------------------------------------------------------------------------
struct Size {
    uint32_t width = 0;
    uint32_t height = 0;
};

struct Rect {
    int32_t x = 0, y = 0;
    int32_t width = 0, height = 0;
};

enum class PixelFormat {
    Unknown,
    BGRA8,   // typical desktop-capture output before color convert
    NV12,    // typical HW-encoder input (4:2:0)
    P010,    // 10-bit / HDR
    I444,    // 4:4:4 (HEVC tier, crisp text)
};

// ---------------------------------------------------------------------------
// A captured frame. The hot path keeps pixels GPU-resident; `native_handle` is
// an opaque OS texture handle (ID3D11Texture2D* / IOSurfaceRef / DMA-BUF fd
// bundle). `cpu_pixels` is populated only by the degraded CPU-readback tiers
// (X11 XShm) or by stub backends.
// ---------------------------------------------------------------------------
struct VideoFrame {
    Size size;
    PixelFormat format = PixelFormat::Unknown;
    uint64_t timestamp_us = 0;       // capture timestamp, microseconds
    void* native_handle = nullptr;   // opaque GPU texture handle (see backend)
    std::vector<uint8_t> cpu_pixels; // populated only on CPU tiers / stubs
    std::vector<Rect> dirty_rects;   // for idle-skip / ROI hints / bandwidth
    bool is_idle = false;            // unchanged-frame marker
};

// Encoded bitstream unit handed to the transport.
struct EncodedPacket {
    std::vector<uint8_t> data;
    uint64_t timestamp_us = 0;
    bool keyframe = false;
};

} // namespace redesk
