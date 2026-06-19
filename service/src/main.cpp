// REDesk service — headless privileged daemon entry point (ADR-001 §2, §3.5).
//
// Responsibilities (per ADR §2): own capture, encode, input injection, transport,
// the Noise E2E session, and the desktop-hop/TCC/portal logic. Expose an
// authenticated, ACL-hardened local IPC channel to the unprivileged UI client.
//
// Modes:
//   --install        register with the OS service manager (SCM/launchd/systemd)
//   --uninstall      remove the registration
//   --run            run as a real OS service (SCM/launchd/systemd hand-off)
//   --foreground     run the daemon inline in this terminal (dev/smoke). In the
//                    default stub build this drives a real capture->encode->
//                    transport smoke pipeline, logging each step.
//
// In the stub build (REDESK_USE_REAL_BACKENDS=OFF) every engine seam is a no-op /
// in-memory implementation, so `redesk-service --foreground` runs end-to-end with
// no Qt/FFmpeg/SDK dependencies. The run loop here is a simple portable loop; the
// real OS service integration lives in ServiceLifecycle::run_as_service().

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

#include "core/common/types.h"
#include "redesk_version.h"
#include "service/src/engine_contracts.h"
#include "service/src/ipc_server.h"
#include "service/src/service_lifecycle.h"

namespace {

using namespace redesk;

// Process-wide stop flag toggled by SIGINT/SIGTERM (foreground) or the service
// control handler (real backends). The run loop polls it.
std::atomic<bool> g_stop{false};

void HandleSignal(int /*sig*/) { g_stop.store(true); }

void InstallSignalHandlers() {
    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);
}

// Bundles every engine object the daemon owns. Constructed once via the core /
// platform factories; outlives the run loop.
struct Daemon {
    std::unique_ptr<capture::ICapturer> capturer;
    std::unique_ptr<codec::IVideoEncoder> encoder;
    std::unique_ptr<input::IInputInjector> injector;
    std::unique_ptr<crypto::IKeyStore> keystore;
    std::unique_ptr<transport::ITransport> transport;
    std::unique_ptr<service::IpcServer> ipc;
    session::SessionManager sessions;
};

// IPC request handler. Every message arrives here already authenticated (peer
// credential check in IpcServer) and size-validated. We still treat the payload
// as untrusted: in the real build this parses the proto/ IPC schema and gates
// each command on the session's negotiated capabilities (ADR §3.5/§3.6.6).
service::IpcReply HandleIpc(Daemon& d, const service::PeerIdentity& peer,
                            const service::IpcMessage& msg) {
    std::cerr << "[ipc] request from " << peer.descriptor << " ("
              << msg.payload.size() << " bytes)\n";
    // TODO(ADR §3.5): decode proto/ command, enforce capability grant, dispatch
    // to the engine (start/stop session, toggle clipboard/input/file-transfer,
    // request keyframe, etc.). Stub echoes a tiny ack.
    (void)d;
    service::IpcReply reply;
    const char* ack = "REDESK-ACK";
    reply.payload.assign(ack, ack + std::strlen(ack));
    return reply;
}

// Construct all engine objects via their factories and bring up the IPC server.
// Returns false on a fatal bring-up error.
bool BuildDaemon(Daemon& d) {
    std::cerr << "[service] REDesk " << kVersion << " — building daemon\n";

    // --- Device identity (ADR §3.6.1) ---------------------------------------
    d.keystore = crypto::CreateKeyStore();
    if (auto fp = d.keystore->device_fingerprint(); fp.ok()) {
        std::cerr << "[service] device fingerprint: " << fp.value << "\n";
    } else {
        std::cerr << "[service] WARN keystore unavailable: "
                  << fp.status.message << "\n";
    }

    // --- Engine factories (core/ + platform/) -------------------------------
    d.capturer = capture::CreateCapturer();
    d.encoder = codec::CreateVideoEncoder();
    d.injector = input::CreateInputInjector();
    d.transport = transport::CreateTransport();

    // --- Transport up (ADR §3.3) --------------------------------------------
    if (Status s = d.transport->start(); !s.ok()) {
        std::cerr << "[service] FATAL transport start failed: " << s.message
                  << "\n";
        return false;
    }

    // --- Capture up (ADR §3.1: primary display) -----------------------------
    if (Status s = d.capturer->start(/*display_index=*/0); !s.ok()) {
        std::cerr << "[service] FATAL capture start failed: " << s.message
                  << "\n";
        return false;
    }

    // --- Encoder config (ADR §3.2 floor: H.264 High 4:2:0) ------------------
    codec::EncoderConfig enc_cfg;
    enc_cfg.resolution = Size{64, 64};  // smoke size; real = capture resolution
    enc_cfg.target_bitrate_bps = 8'000'000;
    enc_cfg.framerate = 60;
    if (Status s = d.encoder->configure(enc_cfg); !s.ok()) {
        std::cerr << "[service] FATAL encoder configure failed: " << s.message
                  << "\n";
        return false;
    }
    std::cerr << "[service] encoder: " << d.encoder->codec_name() << "\n";

    // --- IPC server (ADR §3.5: ACL-hardened, authenticated, untrusted) ------
    d.ipc = service::CreateIpcServer();
    service::IpcServer::Config ipc_cfg;
#if defined(_WIN32)
    ipc_cfg.endpoint = R"(\\.\pipe\REDesk-service)";
#else
    // A fixed, non-per-session path. In production this lives in a root-owned
    // 0755 dir (e.g. /var/run/redesk) so the path itself can't be hijacked.
    ipc_cfg.endpoint = "/tmp/redesk-service.sock";
#endif
    if (Status s = d.ipc->start(
            ipc_cfg,
            [&d](const service::PeerIdentity& p, const service::IpcMessage& m) {
                return HandleIpc(d, p, m);
            });
        !s.ok()) {
        // Non-fatal for the smoke pipeline: log and continue without IPC so the
        // capture/encode/transport loop still demonstrates wiring.
        std::cerr << "[service] WARN IPC server unavailable: " << s.message
                  << "\n";
    } else {
        std::cerr << "[service] IPC listening on " << d.ipc->endpoint() << "\n";
    }

    return true;
}

// One iteration of the media pipeline: capture -> encode -> (paced) transport.
// Logs each step. Returns false to stop the loop on an unrecoverable error.
bool PumpPipeline(Daemon& d, uint64_t tick) {
    auto frame = d.capturer->next_frame();
    if (!frame.ok()) {
        if (frame.status.code == ErrorCode::Again) return true;  // idle, keep going
        std::cerr << "[pump] capture error: " << frame.status.message << "\n";
        return false;
    }

    auto pkt = d.encoder->encode(frame.value);
    if (!pkt.ok()) {
        std::cerr << "[pump] encode error: " << pkt.status.message << "\n";
        return false;
    }

    if (Status s = d.transport->send_video(pkt.value); !s.ok()) {
        std::cerr << "[pump] transport error: " << s.message << "\n";
        return false;
    }

    if ((tick % 30) == 0) {  // throttle logging to ~twice/sec at 60fps cadence
        std::cerr << "[pump] tick=" << tick << " frame=" << frame.value.size.width
                  << "x" << frame.value.size.height
                  << " enc=" << pkt.value.data.size() << "B"
                  << (pkt.value.keyframe ? " [KEY]" : "")
                  << " tx_total=" << d.transport->bytes_sent() << "B\n";
    }
    return true;
}

// The daemon body: bring everything up, run the pipeline loop until asked to
// stop, then tear down cleanly. This is what ServiceLifecycle::run_as_service()
// invokes inside the OS service context (and what --foreground runs directly).
int RunDaemonBody() {
    Daemon d;
    if (!BuildDaemon(d)) {
        return 1;
    }

    // Smoke session so the SessionManager has live state (real build: opened on
    // an authenticated remote peer via the transport handshake, ADR §3.6.6).
    std::string peer = "smoke-peer";
    if (auto sid = d.sessions.open_session(peer); sid.ok()) {
        std::cerr << "[service] opened session " << sid.value << " for " << peer
                  << " (active=" << d.sessions.active_sessions() << ")\n";
    }

    std::cerr << "[service] entering run loop (Ctrl-C / SIGTERM to stop)\n";

    // Portable run loop. TODO(ADR §2): under a real OS service this cooperates
    // with the SCM/launchd/systemd control channel (SERVICE_STOP_PENDING /
    // SIGTERM / sd_notify STOPPING=1). The cadence here approximates 60 fps;
    // the real pipeline is event-driven off capture frame-ready callbacks.
    using clock = std::chrono::steady_clock;
    const auto frame_period = std::chrono::microseconds(16'666);
    uint64_t tick = 0;
    // Cap the stub smoke run so `--foreground` in CI terminates on its own;
    // when driven as a real service the loop runs until g_stop.
    constexpr uint64_t kMaxStubTicks = 180;  // ~3s @ 60fps
    while (!g_stop.load() && tick < kMaxStubTicks) {
        const auto start = clock::now();
        if (!PumpPipeline(d, tick)) break;
        ++tick;
        std::this_thread::sleep_until(start + frame_period);
    }

    std::cerr << "[service] stopping (ran " << tick << " ticks, "
              << d.transport->bytes_sent() << " bytes sent)\n";

    // Teardown in reverse dependency order.
    d.sessions.close_session(1);
    if (d.ipc) d.ipc->stop();
    if (d.transport) d.transport->stop();
    if (d.capturer) d.capturer->stop();

    std::cerr << "[service] clean shutdown\n";
    return 0;
}

void PrintUsage(const char* argv0) {
    std::cerr
        << "REDesk service " << kVersion << "\n"
        << "usage: " << argv0 << " [--install|--uninstall|--run|--foreground]\n"
        << "  --install     register with the OS service manager\n"
        << "  --uninstall   remove the OS service registration\n"
        << "  --run         run as an OS service (SCM/launchd/systemd hand-off)\n"
        << "  --foreground  run inline (dev/smoke pipeline)\n";
}

std::string AbsoluteSelfPath(const char* argv0) {
    // Best-effort; the real install path check (ADR §3.4 trusted-path) belongs
    // in the lifecycle backend, not here.
    return argv0 ? std::string(argv0) : std::string("redesk-service");
}

}  // namespace

int main(int argc, char** argv) {
    InstallSignalHandlers();

    std::string mode = (argc > 1) ? argv[1] : "--foreground";

    auto lifecycle = service::CreateServiceLifecycle();
    service::ServiceInstallSpec spec;
    spec.executable_path = AbsoluteSelfPath(argv[0]);
    std::cerr << "[service] lifecycle backend: " << lifecycle->backend_name()
              << "\n";

    if (mode == "--install") {
        Status s = lifecycle->install(spec);
        std::cerr << "[service] install: "
                  << (s.ok() ? "ok" : ("failed: " + s.message)) << "\n";
        return s.ok() ? 0 : 1;
    }
    if (mode == "--uninstall") {
        Status s = lifecycle->uninstall(spec);
        std::cerr << "[service] uninstall: "
                  << (s.ok() ? "ok" : ("failed: " + s.message)) << "\n";
        return s.ok() ? 0 : 1;
    }
    if (mode == "--run") {
        // Real OS service hand-off; the body runs inside the service context.
        return lifecycle->run_as_service(spec, [] { return RunDaemonBody(); });
    }
    if (mode == "--foreground") {
        return RunDaemonBody();
    }

    PrintUsage(argv[0]);
    return 2;
}
