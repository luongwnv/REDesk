#pragma once

// REDesk service — engine contract adapter (ADR-001 §3.1/§3.2/§3.3/§3.4/§3.6).
//
// The service links REDesk::core + REDesk::platform and drives the
// capture -> encode -> transport pipeline plus input injection, the session
// manager, and the device keystore. Those interfaces + their factories are
// owned by the core/ and platform/ slices (written in parallel).
//
// To keep the service's default stub build green and self-contained REGARDLESS
// of whether the sibling slices have landed yet, this header does the following:
//
//   * If REDESK_HAVE_ENGINE_HEADERS is defined (set by service/CMakeLists.txt
//     once the real core/platform headers are present), it includes the real
//     interface headers and the service wires against them verbatim.
//
//   * Otherwise it provides a SMALL, FAITHFUL local mirror of the expected
//     interfaces + factory declarations in the same redesk sub-namespaces, with
//     stub implementations defined inline so `redesk-service --foreground` runs a
//     real smoke pipeline today. These mirrors are deliberately minimal and are
//     the contract the integrator should reconcile against the real core/platform
//     signatures (see the summary returned by this slice).
//
// WHY this shape: the assignment requires main.cpp to *actually construct* the
// engine objects via their factories and start the (stub) pipeline. We cannot
// hard-include headers that may not exist without breaking HARD RULE 2 (stub
// build must compile with no missing deps). This adapter is the seam.

#include <memory>
#include <string>
#include <vector>

#include "core/common/types.h"

#if defined(REDESK_HAVE_ENGINE_HEADERS)

// --- Real slices present: use the canonical interfaces + factories. ---------
// NOTE(integrator): confirm these include paths/factory names match what the
// core/ and platform/ slices actually export; adjust here only (single seam).
#include "core/capture/capturer.h"        // redesk::capture::ICapturer, CreateCapturer
#include "core/codec/video_encoder.h"     // redesk::codec::IVideoEncoder, CreateVideoEncoder
#include "core/input/input_injector.h"    // redesk::input::IInputInjector, CreateInputInjector
#include "core/crypto/keystore.h"         // redesk::crypto::IKeyStore, CreateKeyStore
#include "core/transport/transport.h"     // redesk::transport::ITransport, CreateTransport
#include "core/session/session_manager.h" // redesk::session::SessionManager

#else  // !REDESK_HAVE_ENGINE_HEADERS — local faithful mirror + inline stubs.

namespace redesk::capture {

// Mirror of core ICapturer (ADR §3.1): start/stop + frame pull. The real
// interface delivers GPU-resident frames + a separate cursor channel; the stub
// synthesizes a CPU frame so the pipeline has something to encode.
class ICapturer {
public:
    virtual ~ICapturer() = default;
    virtual Status start(uint32_t display_index) = 0;
    virtual void stop() = 0;
    // Pull the next frame (blocking-ish in real backends; immediate in stub).
    virtual Result<VideoFrame> next_frame() = 0;
};

std::unique_ptr<ICapturer> CreateCapturer();

}  // namespace redesk::capture

namespace redesk::codec {

struct EncoderConfig {
    Size resolution;
    uint32_t target_bitrate_bps = 8'000'000;
    uint32_t framerate = 60;
};

// Mirror of core IVideoEncoder (ADR §3.2): H.264 High 4:2:0 floor in real
// builds; the stub emits a length-tagged fake bitstream so transport has bytes.
class IVideoEncoder {
public:
    virtual ~IVideoEncoder() = default;
    virtual Status configure(const EncoderConfig& cfg) = 0;
    virtual Result<EncodedPacket> encode(const VideoFrame& frame) = 0;
    virtual std::string codec_name() const = 0;
};

std::unique_ptr<IVideoEncoder> CreateVideoEncoder();

}  // namespace redesk::codec

namespace redesk::input {

struct KeyEvent {
    uint32_t scancode = 0;
    bool down = false;
};
struct PointerEvent {
    int32_t x = 0, y = 0;
    bool absolute = true;
};

// Mirror of core IInputInjector (ADR §3.4). Stub logs/no-ops.
class IInputInjector {
public:
    virtual ~IInputInjector() = default;
    virtual Status inject_pointer(const PointerEvent& ev) = 0;
    virtual Status inject_key(const KeyEvent& ev) = 0;
};

std::unique_ptr<IInputInjector> CreateInputInjector();

}  // namespace redesk::input

namespace redesk::crypto {

// Mirror of core IKeyStore (ADR §3.6): the per-device long-lived X25519 static
// identity, stored in the OS keystore. Stub keeps an in-memory placeholder key.
class IKeyStore {
public:
    virtual ~IKeyStore() = default;
    // Load (or first-run generate) the device static public key. Returns the
    // BLAKE2b "safety number" fingerprint as the identity (ADR §3.6.1).
    virtual Result<std::string> device_fingerprint() = 0;
};

std::unique_ptr<IKeyStore> CreateKeyStore();

}  // namespace redesk::crypto

namespace redesk::transport {

// Mirror of core ITransport (ADR §3.3): custom UDP over an ICE-managed socket
// with a packet pacer + keyframe governor. Stub accounts bytes only.
class ITransport {
public:
    virtual ~ITransport() = default;
    virtual Status start() = 0;
    virtual void stop() = 0;
    // Hand an encoded media unit to the (paced) sender for the video channel.
    virtual Status send_video(const EncodedPacket& pkt) = 0;
    virtual uint64_t bytes_sent() const = 0;
};

std::unique_ptr<ITransport> CreateTransport();

}  // namespace redesk::transport

namespace redesk::session {

// Mirror of core SessionManager (ADR §3.6.6): owns per-session capability grants
// and lifecycle. The stub tracks a session count so the daemon has real state.
class SessionManager {
public:
    SessionManager() = default;

    // Begin a session for an authenticated remote peer (fingerprint identity).
    // Real impl runs the in-channel auth + capability negotiation; stub admits.
    Result<uint64_t> open_session(const std::string& peer_fingerprint) {
        const uint64_t id = ++next_id_;
        ++active_;
        last_peer_ = peer_fingerprint;
        return Result<uint64_t>::good(id);
    }

    void close_session(uint64_t /*id*/) {
        if (active_ > 0) --active_;
    }

    uint64_t active_sessions() const { return active_; }
    const std::string& last_peer() const { return last_peer_; }

private:
    uint64_t next_id_ = 0;
    uint64_t active_ = 0;
    std::string last_peer_;
};

}  // namespace redesk::session

#endif  // REDESK_HAVE_ENGINE_HEADERS
