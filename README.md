# REDesk

Native cross-platform remote desktop for **Windows · macOS · Linux**, built in **C++ / Qt 6 (QML)** and targeting **AnyDesk-class smoothness** and visual design. Comparable in scope to [RustDesk](https://github.com/rustdesk/rustdesk), Chrome Remote Desktop, and AnyDesk.

> **Status: architecture + scaffold (Phase 0).** The full monorepo is wired and the default *stub build* compiles and passes its test suite with **no external dependencies** (no Qt/FFmpeg/vendor SDKs). Real backends are filled in per-layer behind `REDESK_USE_REAL_BACKENDS`.

## Why it's structured this way

Every architectural decision — capture, codec, transport/NAT, input injection, UI, security — was researched and adversarially verified, then recorded in **[docs/adr/ADR-001-architecture.md](docs/adr/ADR-001-architecture.md)**. Read that first; the source tree mirrors it section-for-section.

Headline decisions:

| Layer | Decision |
|---|---|
| **Capture** | DXGI Desktop Duplication + WGC (Win) · ScreenCaptureKit (mac) · PipeWire-portal + X11 fallback (Linux) — GPU-resident, separate cursor channel |
| **Codec** | Tiered, runtime-probed: H.264 floor → HEVC (4:4:4 text) → AV1; vendor SDKs direct (NVENC/AMF/QSV/MF · VideoToolbox · VAAPI) |
| **Transport** | Custom UDP over ICE (libjuice) · GCC congestion control + **mandatory pacer + keyframe governor** · thin blind-forward relay fallback |
| **Input** | SendInput (Win) · CGEvent (mac) · runtime capability ladder portal+libei → wlroots → XTEST (Linux) |
| **UI** | Qt Quick / QML, AnyDesk-style; decoded frames via `QQuickRhiItem` (zero-copy), decoupled from hard vsync |
| **Security** | E2E Noise (XK→KK) on libsodium · zero-knowledge Ed25519 signed-introducer rendezvous · CPace/OPAQUE session auth |

## Repository layout

```
core/        platform-abstracted engine (capture, codec, transport, crypto, input, session) — NO OS code
platform/    one native backend per OS (windows/ macos/ linux/), built into REDesk::platform
ui/          Qt Quick (QML) client — control panel + remote video item
service/     headless privileged daemon (SCM service / LaunchDaemon / systemd)
server/      rendezvous (signaling, zero-knowledge) + relay (blind-forward fallback)
proto/       hand-rolled wire + IPC protocol headers (single source of truth)
tests/       dep-free test harness exercising the stub backends
cmake/        options, version, sanitizers, vendor-SDK locators
third_party/ vendored deps (added as real backends land)
docs/adr/    architecture decision records
```

The two-process model (privileged `service/` + unprivileged `ui/` over authenticated local IPC) and the engine/platform split (`platform/` depends on `core/`; the service injects platform backends into core interfaces) are described in ADR-001 §2.

## Build (stub — no dependencies)

```sh
cmake -S . -B build -G Ninja -DREDESK_USE_REAL_BACKENDS=OFF
cmake --build build
ctest --test-dir build --output-on-failure
```

This builds `redesk-service`, `redesk-rendezvous`, `redesk-relay`, and the test suite on any host with a C++20 compiler. The UI target self-disables if Qt 6 isn't found (a status message tells you). Smoke the daemon with:

```sh
./build/bin/redesk-service --foreground
```

## Build (real backends)

Flip `-DREDESK_USE_REAL_BACKENDS=ON` and provide the per-OS dependencies (Qt 6.8+, FFmpeg LGPL, libsodium, libjuice, and the platform capture/encode SDKs). See ADR-001 §4 for the full library list **with license flags** (note the GPL/patent traps around x264, wolfSSL DTLS 1.3, and H.264/HEVC royalties) and §5–6 for structure and the phased roadmap.

## Roadmap

Phase 0 (this scaffold) → **Phase 1: LAN MVP** (single-monitor H.264, direct UDP, Noise, basic input, QML video) → Phase 2: NAT traversal + relay → Phase 3: codec & smoothness upgrades → Phase 4: file transfer/clipboard/audio → Phase 5: unattended access & elevation → Phase 6: hardening, PQ crypto, scale. Details in ADR-001 §6.
