// REDesk service — fallback engine stub factories (ADR-001 §3.1-§3.6).
//
// These definitions exist ONLY when the real core/platform slices are not yet
// available to the service (REDESK_HAVE_ENGINE_HEADERS undefined). They let
// `redesk-service --foreground` run an end-to-end smoke pipeline today:
//
//     CreateCapturer()  -> a synthetic BGRA frame
//     CreateVideoEncoder() -> a fake length-tagged "bitstream" packet
//     CreateTransport() -> byte accounting (no socket)
//     CreateInputInjector() / CreateKeyStore() -> safe no-ops / in-memory key
//
// When REDesk::core + REDesk::platform provide the real factories, this whole
// translation unit compiles to nothing and the service links the real symbols.
// TODO(integrator): delete this file once core/platform export the factories and
// REDESK_HAVE_ENGINE_HEADERS is set in service/CMakeLists.txt.

#include "service/src/engine_contracts.h"

#if !defined(REDESK_HAVE_ENGINE_HEADERS)

#include <cstring>

namespace redesk::capture {
namespace {
class StubCapturer final : public ICapturer {
public:
    Status start(uint32_t display_index) override {
        display_ = display_index;
        running_ = true;
        return Status::success();
    }
    void stop() override { running_ = false; }

    Result<VideoFrame> next_frame() override {
        if (!running_) {
            return Result<VideoFrame>::fail(ErrorCode::Again, "capturer stopped");
        }
        // Synthesize a small BGRA frame so the encoder has real bytes to chew.
        // TODO(ADR §3.1): real backend = DXGI DDA / ScreenCaptureKit /
        // PipeWire-portal delivering GPU-resident textures + cursor channel.
        VideoFrame f;
        f.size = Size{64, 64};
        f.format = PixelFormat::BGRA8;
        f.timestamp_us = (++seq_) * 16'666;  // ~60 fps cadence
        f.cpu_pixels.assign(static_cast<size_t>(f.size.width) * f.size.height * 4,
                            static_cast<uint8_t>(seq_ & 0xFF));
        f.is_idle = false;
        return Result<VideoFrame>::good(std::move(f));
    }

private:
    uint32_t display_ = 0;
    uint64_t seq_ = 0;
    bool running_ = false;
};
}  // namespace
std::unique_ptr<ICapturer> CreateCapturer() {
    return std::make_unique<StubCapturer>();
}
}  // namespace redesk::capture

namespace redesk::codec {
namespace {
class StubEncoder final : public IVideoEncoder {
public:
    Status configure(const EncoderConfig& cfg) override {
        cfg_ = cfg;
        return Status::success();
    }
    Result<EncodedPacket> encode(const VideoFrame& frame) override {
        // TODO(ADR §3.2): real backend = NVENC/AMF/QSV/MF/VideoToolbox/VAAPI
        // H.264 High 4:2:0 floor with low-latency RC, pacer-friendly governor.
        EncodedPacket p;
        // Fake "bitstream": header byte + a quarter of the source bytes, with a
        // periodic keyframe so transport sees realistic keyframe cadence.
        p.keyframe = (frame_count_ % 60) == 0;
        p.timestamp_us = frame.timestamp_us;
        const size_t take = frame.cpu_pixels.size() / 4 + 1;
        p.data.reserve(take + 1);
        p.data.push_back(p.keyframe ? static_cast<uint8_t>(0xFF)
                                    : static_cast<uint8_t>(0xAA));
        p.data.insert(p.data.end(), frame.cpu_pixels.begin(),
                      frame.cpu_pixels.begin() +
                          static_cast<std::ptrdiff_t>(take));
        ++frame_count_;
        return Result<EncodedPacket>::good(std::move(p));
    }
    std::string codec_name() const override { return "stub-h264-floor"; }

private:
    EncoderConfig cfg_;
    uint64_t frame_count_ = 0;
};
}  // namespace
std::unique_ptr<IVideoEncoder> CreateVideoEncoder() {
    return std::make_unique<StubEncoder>();
}
}  // namespace redesk::codec

namespace redesk::input {
namespace {
class StubInjector final : public IInputInjector {
public:
    Status inject_pointer(const PointerEvent&) override {
        // TODO(ADR §3.4): SendInput / CGEventPost / portal+libei.
        return Status::success();
    }
    Status inject_key(const KeyEvent&) override { return Status::success(); }
};
}  // namespace
std::unique_ptr<IInputInjector> CreateInputInjector() {
    return std::make_unique<StubInjector>();
}
}  // namespace redesk::input

namespace redesk::crypto {
namespace {
class StubKeyStore final : public IKeyStore {
public:
    Result<std::string> device_fingerprint() override {
        // TODO(ADR §3.6.1): generate/load the X25519 static key from the OS
        // keystore (Keychain/DPAPI-CNG/libsecret) and return its BLAKE2b safety
        // number. This placeholder is stable-per-process only.
        return Result<std::string>::good("STUB-FINGERPRINT-0000-0000-0000-0000");
    }
};
}  // namespace
std::unique_ptr<IKeyStore> CreateKeyStore() {
    return std::make_unique<StubKeyStore>();
}
}  // namespace redesk::crypto

namespace redesk::transport {
namespace {
class StubTransport final : public ITransport {
public:
    Status start() override {
        running_ = true;
        return Status::success();
    }
    void stop() override { running_ = false; }
    Status send_video(const EncodedPacket& pkt) override {
        if (!running_) {
            return Status::error(ErrorCode::ConnectionLost, "transport stopped");
        }
        // TODO(ADR §3.3): real backend = custom UDP over ICE socket with GCC
        // congestion control + mandatory packet pacer + keyframe-size governor,
        // Noise-encrypted. Here we just account bytes.
        bytes_ += pkt.data.size();
        return Status::success();
    }
    uint64_t bytes_sent() const override { return bytes_; }

private:
    uint64_t bytes_ = 0;
    bool running_ = false;
};
}  // namespace
std::unique_ptr<ITransport> CreateTransport() {
    return std::make_unique<StubTransport>();
}
}  // namespace redesk::transport

#endif  // !REDESK_HAVE_ENGINE_HEADERS
