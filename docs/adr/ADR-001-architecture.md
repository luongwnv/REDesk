# REDesk — Architecture Decision Record (ADR-001)

**Status:** Accepted · **Date:** 2026-06-19 · **Owner:** Lead Architect
**Scope:** Cross-platform (Windows · macOS · Linux) remote desktop targeting AnyDesk-class smoothness and visual polish.

> This ADR was produced from a multi-agent research pass (6 technical layers, each adversarially verified). It is the source of truth for the scaffold in this repo. Each `core/` interface and `platform/` backend in the tree maps directly to a decision below.

---

## 1. Executive Summary

REDesk is a native C++/Qt application built as **two processes per machine**: a headless, privileged **service/daemon** (Windows SCM service / macOS LaunchDaemon / Linux systemd unit) that owns capture, encode, input injection, and the network transport; and a **Qt Quick (QML) UI client** that renders the local control panel and the remote video, talking to the service over an authenticated local IPC channel (named pipe / Unix domain socket). The media pipeline is **GPU-resident end-to-end with a GPU color-space convert (BGRA→NV12/P010) and no CPU readback**: DXGI Desktop Duplication (Windows) / ScreenCaptureKit (macOS) / PipeWire-portal (Linux) feed hardware encoders (NVENC/AMF/QSV/MF · VideoToolbox · VAAPI) via the vendor SDKs directly. The codec stack is **codec-agnostic with tiered negotiation: H.264 High 4:2:0 as the always-available HW floor, HEVC as the preferred upgrade (and the only tier carrying 4:4:4 for crisp text), AV1 opportunistically when both ends pass a runtime probe.** Transport is a **custom UDP protocol over an ICE-managed socket** (libjuice for NAT traversal, thin blind-forward relays + optional coturn/eturnal as the universal fallback), with **GCC-style delay-based congestion control plus a mandatory packet pacer and keyframe-size governor.** Security is **end-to-end via the Noise Protocol Framework** (XK first-contact → KK pinned), with libsodium as the single crypto core, an Ed25519 signed-introducer rendezvous (zero-knowledge of media keys), TOFU + out-of-band fingerprint verification, and Argon2id-hashed unattended passwords proven inside the encrypted channel via a balanced PAKE (CPace). Decoded frames are presented zero-copy under the QML chrome through a `QQuickRhiItem`, with the live-video surface **decoupled from hard local vsync** for minimum glass-to-glass latency.

---

## 2. High-Level Architecture (ASCII)

```
        CONTROLLER MACHINE (viewer)                          HOST MACHINE (controlled)
 ┌───────────────────────────────────────┐          ┌───────────────────────────────────────┐
 │  UI CLIENT PROCESS (Qt Quick / QML)    │          │  UI CLIENT PROCESS (Qt Quick / QML)     │
 │   • ID address bar, session toolbar    │          │   • consent dialogs, "being controlled" │
 │   • remote video via QQuickRhiItem     │          │     indicator, permission toggles       │
 │   • HW decode (FFmpeg libavcodec +     │          │   • local control panel                 │
 │     D3D11VA/VideoToolbox/VAAPI)        │          │                                         │
 └──────────────┬────────────────────────┘          └───────────────────┬─────────────────────┘
                │ local IPC (QLocalSocket: named pipe / UDS)             │ local IPC (ACL-hardened,
                │ + authenticated, ACL-restricted, peer-validated        │  peer-validated)
 ┌──────────────┴────────────────────────┐          ┌───────────────────┴─────────────────────┐
 │  SERVICE / DAEMON (privileged)         │          │  SERVICE / DAEMON (privileged)          │
 │   • transport (ICE+custom UDP)         │          │   • capture (DDA/SCK/PipeWire)          │
 │   • Noise E2E session                  │          │   • HW encode (NVENC/AMF/QSV/MF/VT/VAAPI)│
 │   • input INJECTION                    │          │   • input injection backends            │
 │   • desktop-hop / TCC / portal logic   │          │   • Noise E2E session                   │
 └──────────────┬────────────────────────┘          └───────────────────┬─────────────────────┘
                │                                                        │
                │        ┌──────────────────────────────────┐           │
                │   (1)  │  RENDEZVOUS / SIGNALING SERVER     │   (1)     │
                ├───────►│  • ID→endpoint registry            │◄──────────┤
                │ control│  • brokers ICE candidates / SDP    │ control   │
                │ conn    │  • Ed25519-signs peer static pubkey│ conn      │
                │        │  • ZERO knowledge of media keys     │           │
                │        │  • hands back lowest-RTT relay      │           │
                │        └──────────────────────────────────┘           │
                │                                                        │
                │   (2) ICE hole-punch ─ DIRECT P2P MEDIA PATH (preferred)│
                ╞════════════════════════════════════════════════════════╡
                │        Noise-encrypted UDP: video / audio (unreliable, FEC+NACK)
                │                            input/control/clipboard/files (reliable, per-channel)
                │                                                        │
                │   (3) FALLBACK when punch fails:                        │
                │        ┌──────────────────────────────────┐           │
                └───────►│  RELAY SERVER (thin blind-forward) │◄──────────┘
                         │  • forwards opaque Noise ciphertext│
                         │  • multi-region / anycast          │
                         │  • coturn/eturnal only for std TURN│
                         │    (web-client/libdatachannel path)│
                         └──────────────────────────────────┘

 Legend: (1) signaling/brokering only · (2) preferred encrypted P2P · (3) encrypted relay fallback (~15–30%+ of sessions)
```

---

## 3. Final Decisions Per Layer

### 3.1 Screen Capture

One abstract `ICapturer` interface (start/stop, enumerate displays, deliver GPU-resident frames + a **separate cursor channel**) with three native backends. Hot path keeps frames on the GPU.

- **Windows — Primary: DXGI Desktop Duplication (DDA).** One `IDXGIOutputDuplication` per output (`IDXGIOutput5::DuplicateOutput1` for HDR/FP16). `AcquireNextFrame` returns a D3D11 texture + `DXGI_OUTDUPL_FRAME_INFO` (dirty/move rects, cursor pos); pull cursor via `GetFramePointerShape` onto its own channel.
  - **Correction (lifecycle):** the duplication surface is valid only until `ReleaseFrame`. You **must `CopyResource`/color-convert into an app-owned NV12 texture, then `ReleaseFrame` immediately**, then encode. Do **not** hold the duplication surface across the encode.
  - **Correction (ACCESS_LOST):** handle `DXGI_ERROR_ACCESS_LOST` (mode/desktop/secure-desktop/fullscreen switch) by tearing down and re-duplicating.
  - **Fallback: WGC** for single-window capture, RDP/virtual displays, locked-down sessions, and under GPU saturation. Frames are also D3D11 textures (shared encoder path). Gate `IsBorderRequired=false` on **Win11 build 20348+** (cannot remove the capture border on Win10) and `IsCursorCaptureEnabled` on Win10 2004+, via `ApiInformation`.
  - Protected/DRM content returns black on **both** DDA and WGC — expected behavior, no workaround.
- **macOS — ScreenCaptureKit only** (`SCStream`/`SCStreamConfiguration`/`SCContentFilter`). IOSurface-backed `CVPixelBuffer` feeds VideoToolbox zero-copy; one `SCStream` per `SCDisplay`. Configure Retina scale, color space (P3/HDR), `minimumFrameInterval=1/60`, cursor mode.
  - **Correction (TCC, high confidence):** Screen Recording must be **re-confirmed by the user roughly monthly and after every reboot** on macOS 15/26. The only mitigation for unattended remote-support is **MDM-delivered PPPC/TCC profiles** (user-approved/supervised) plus a **properly signed, notarized `.app` bundle** — a bare executable will **not appear in the Screen Recording list on macOS 26.1+**. Budget real engineering for re-grant UX.
  - **Correction (pre-login, high confidence):** SCK **fails at the macOS Login Window** from a system daemon (null buffer, no graphical context). macOS login-screen unattended control is a **research risk, not a settled feature** — scope to post-login capture; investigate auto-login + privileged helper and validate on Sequoia/Tahoe before committing.
- **Linux — Wayland primary** via `org.freedesktop.portal.ScreenCast` (CreateSession → SelectSources → Start). Negotiate **`SPA_DATA_DmaBuf`** and import into VAAPI/EGL. **Persist `restore_token`** (single-use, rotated each Start; write atomically) to avoid re-prompts.
  - **Correction:** DMA-BUF is **best-effort with a robust MemFd/SHM fallback** — validate DRM format modifiers against the encoder's importer; handle multi-GPU (capture vs encode device) mismatch.
  - **X11 fallback (compatibility floor only):** XShm via `XShmGetImage` + XFixes cursor. This is explicitly a **CPU-bound, higher-latency tier** that violates the "no CPU readback" invariant — labeled and capped accordingly. DRM/KMS is privileged and cursor-incomplete; prefer the portal.

**Cross-cutting:** dirty/move rects are used for **idle-frame skipping, ROI/QP hints, and bandwidth** — **not** partial-frame HW encoding (mainstream HW encoders encode full frames). Unify all three OS cursor models into one cursor channel. Maintain an **OS-version/HW test matrix** (macOS Sequoia vs Tahoe HDR/high-DPI external-display stutter; Windows fullscreen-game GPU saturation → `AcquireNextFrame` stalls → raise capture-thread priority / WGC fallback under load).

### 3.2 Encode / Decode

Codec-agnostic `IVideoEncoder`/`IVideoDecoder` with **tiered, bidirectionally-negotiated, runtime-probed** codec selection. **Every HW path is gated by a real test-encode/test-decode of one frame at the target profile/chroma — never by marketing tier names.**

1. **Floor (always present): H.264 High 4:2:0 8-bit, hardware.** Never ship without a working HW H.264 path on both ends.
2. **Preferred upgrade: HEVC** (4:2:0; **4:4:4 bound exclusively here** for crisp text — H.264 4:4:4 has **zero HW decode on any vendor** and is dropped entirely). Negotiate when both ends pass HW encode/decode probes. ~25–40% bitrate win.
3. **Opportunistic: AV1 (HW only)**, enabled only when sender HW-encodes and receiver HW-decodes per probe (covers low-end Ampere gaps). ~30–50% win for constrained uplinks. No software AV1 for real-time.
4. **SW fallback:** for the **High-profile** floor use **x264 (GPL/commercial)** or a platform SW H.264 encoder. **openh264 is Constrained-Baseline-only** and cannot match a negotiated High profile — when openh264 is in play, **negotiate the profile down to a baseline-compatible profile on both ends.** Skip VP9 unless browser interop forces it.

**Abstraction:** call the **vendor SDKs directly** (NVENC, AMF, Intel oneVPL/QSV, VideoToolbox, Media Foundation) for low-latency control and to keep the binary license-clean. Avoid routing HW encoders through FFmpeg: **NVENC requires FFmpeg `--enable-nonfree`, producing an un-redistributable binary** — the "LGPL FFmpeg stays closed-source-clean" claim is false for these encoders. Use libavcodec only for genuinely LGPL-safe SW paths / decode plumbing where it doesn't taint the binary; audit the effective per-platform license.

**Low-latency config:** zero B-frames, single ref, low-latency preset (NVENC p1–p4 + tune ull; VideoToolbox `RealTime=true`); **CBR/capped-VBR with tight VBV (~1 frame, ≤250 ms)**; slice-based; no look-ahead; no scene-cut detection. **Loss recovery is per-backend, not a global default:** NVENC → true rolling intra-refresh + reference-frame invalidation; QSV/AMF/VAAPI → rolling intra-refresh where the driver exposes it, else periodic IDR with tight VBV; **VideoToolbox → LTR frames + `ForceLTRRefresh`** (no classic intra-refresh). Prefer intra-refresh/LTR/ref-invalidation over forced full IDRs on loss.

**macOS specifics:** HEVC is the practical default; **runtime-probe VideoToolbox for AV1 encode** (present on newest high-end Apple Silicon, e.g. M5 Pro/Max) rather than hard-coding "H.264/HEVC only." For low latency use `EnableLowLatencyRateControl` with `averageBitRate` + `DataRateLimits` (ABR + hard cap); **do NOT set `kVTCompressionPropertyKey_ConstantBitRate`** (incompatible with low-latency RC for H.264). `AllowFrameReordering=false`.

**Screen-content:** sharpness on the H.264 floor comes from **full-range 4:2:0 + dirty-rects + intra-refresh + adequate bitrate**; reserve 4:4:4 for the HEVC tier. Do **not** depend on HEVC-SCC palette/intra-block-copy or AV1 SCC tools (not exposed by real-time HW encoders in 2026).

### 3.3 Transport + NAT

Custom UDP transport over an **ICE-managed socket**; do **not** adopt libwebrtc media or QUIC as the primary video path.

- **NAT traversal:** full ICE (RFC 8445) via **libjuice**; tiers host/IPv6 → server-reflexive (STUN punch) → relayed (TURN). **Plan TURN/relay capacity for 15–30%+ of sessions** (CGNAT/symmetric-NAT/enterprise reality), not the optimistic ~3%. Support TCP/TLS-443 relay fallback where UDP is blocked.
- **Transport over one UDP 5-tuple:** multiplexed channels.
  - **Video/audio:** unreliable/partially-reliable; per-frame deadline, drop stale; FEC for steady loss + tight selective-NACK (retransmit only if it can arrive before the frame deadline); keyframe/LTR request on unrecoverable loss.
  - **Input/control/clipboard/file-transfer:** fully reliable, **independent per-channel ordered streams** (no head-of-line blocking of input behind a file transfer). Input → immediate ARQ.
- **Congestion control:** delay-based **GCC** (trendline/Kalman gradient via transport-wide-cc) + loss controller → target bitrate driving the encoder per RTT. **Not BBR** for interactive video (BBR only for the bulk file-transfer channel). **Mandatory: a packet pacer + keyframe-size governor** between encoder and socket, with a clamp on max frame size — without pacing, large scene-change frames burst, GCC mis-measures queuing delay and over-throttles (the classic "judder on screen change"). This is a smoothness requirement.
- **Precedents:** lead with **Parsec (BUD = UDP + DTLS 1.2 + custom CC)** and **Moonlight/Sunshine (UDP + FEC, drop-late)**. Cite RustDesk for the **two-server hbbs/hbbr topology**, but its core transport is a custom length-framed protocol with sodiumoxide secretbox — **not "KCP."**
- **Server topology:** (a) lightweight rendezvous (ID→endpoint registry, brokers ICE, signs pubkeys, zero media-key knowledge); (b) **thin blind-forward relay as the default punch-fail path**, coturn/**eturnal**/STUNner only where standards TURN is needed for a web-client/libdatachannel fallback. Both horizontally scalable, multi-region/anycast.
- **libdatachannel** is an optional standards/web-client fallback only — it lacks GCC/BWE, NACK/PLI, RTX, and screen-content packetization, so it is a **genuinely lower quality tier**, not a drop-in fallback.

### 3.4 Input Injection

Abstract `IInputInjector` (relative+absolute move, buttons, hi-res scroll, HID-scancode keys, separate Unicode path, modifier set/reset) + `IClipboardSync`. **Linux backend selected at runtime by capability probe, never compile-time.**

- **Windows:** single `SendInput()` per event batch. Mouse: `MOUSEEVENTF_MOVE|ABSOLUTE|VIRTUALDESK`, normalized with the **negative-origin offset**: `x = round((x - SM_XVIRTUALSCREEN) * 65535 / (SM_CXVIRTUALSCREEN - 1))` (same for y) — verify the cursor reaches the extreme bottom-right pixel and monitors left/above primary. Keyboard: `KEYEVENTF_SCANCODE` (+`EXTENDEDKEY`); arbitrary text via `KEYEVENTF_UNICODE`. Scroll in 120-unit deltas.
  - **Correction (split the privilege claim):** (1) elevated + `uiAccess="true"` + Authenticode-signed + **trusted install path** (Program Files/system32) → SendInput can drive higher-integrity **normal** windows. (2) The **Secure Desktop** (UAC consent, logon, lock) is reachable **only** from a SYSTEM session-0 service that calls `OpenInputDesktop`/`SetThreadDesktop` to the current input desktop on WTS session-change/desktop-switch. **uiAccess alone does not reach the Secure Desktop.**
- **macOS:** Quartz `CGEventCreate*` + `CGEventPost`. Scroll via `CGEventCreateScrollWheelEvent2` (pixel for smooth, line for legacy/some Electron/AppKit targets). Unicode via `CGEventKeyboardSetUnicodeString`.
  - **Correction (modifiers):** do **not** rely solely on `CGEventSetFlags` + `kCGHIDEventTap` for held-modifier chords (it drops flags in some cases). Either post explicit modifier key-down/up around the chord, or use `kCGSessionEventTap` when flag fidelity matters and reserve `kCGHIDEventTap` for physical-device semantics (games/fullscreen). Test Cmd+Tab, Shift+drag on Sequoia/Tahoe.
  - Requires **Accessibility** TCC; gate at runtime with `CGPreflightPostEventAccess()` (do **not** trust `AXIsProcessTrusted`). Ship the injector in a normally-launched / `SMAppService` helper (Sequoia background-helper regression). Distribute **Developer ID, not MAS** (sandbox breaks PID taps).
- **Linux capability ladder:** RemoteDesktop portal + **libei (`ConnectToEIS`)** → wlroots `virtual-pointer`/`virtual-keyboard` → (Xorg) XTEST.
  - **Correction (support floor):** consumable portal `ConnectToEIS`+libei is **GNOME ≥46 / Mutter 46 (Xwayland 23.2) and KDE Plasma ≥6.1** — not "~GNOME 45." Probe `ConnectToEIS` availability, don't infer from name/version.
  - **Correction (wlroots keyboard):** avoid per-character dynamic xkb remap — upload **one comprehensive keymap once at session start** (per-char remap desyncs the compositor keymap → invalid keycodes until a physical keypress; swaywm #2420, labwc #3113). Prefer `NotifyKeyboardKeysym`/libei keysym injection wherever available.
  - **Latency expectation:** Wayland (portal+libei) injection has **measurably higher end-to-end latency** than Windows SendInput / macOS HID tap / X11 XTEST. Surface "input unsupported on this compositor" gracefully; wlroots no-consent protocols are unprivileged-but-unsandboxed and may be disabled — rely on **your own authz gate**.
  - `restore_token`: write **atomically (temp + rename) immediately after each successful Start, before sending input**; treat re-consent as a recoverable state.
- **Clipboard:** separate module. Implement text/image/uri-list via native APIs (`Win32 clipboard` `CF_UNICODETEXT`/`CF_DIBV5`/`CF_HDROP`; `NSPasteboard`; `wl_data_control`/`zwlr_data_control_manager_v1` or `QClipboard` on Linux). **Stream file contents over your own channel and synthesize virtual files on paste** (`IDataObject`/`CFSTR_FILECONTENTS`, `NSFilePromiseProvider`) — never share local paths. Given LGPL weight, prefer native clipboard APIs over pulling in Qt just for MIME handling.

### 3.5 UI

- **Qt Quick / QML** (not Widgets), **Qt 6.8 LTS** baseline → migrate to **6.12 LTS** when ready.
- **Two-process split** (privileged service + UI client) as in §2. IPC = `QLocalSocket`.
  - **Correction (IPC hardening):** set an **explicit security descriptor** on the service's named pipe — grant only intended user SID(s)/Administrators/SYSTEM, **deny Everyone/anonymous**; do not place it in the per-session object namespace. **Authenticate the UI↔service channel** (UI is unprivileged and impersonable); validate every message as untrusted.
- **Video presentation:** decode with **raw libavcodec** (low-delay flags) using HW decoders (D3D11VA / VideoToolbox / VAAPI-Vulkan); present zero-copy via `QQuickRhiItem` wrapping the native texture with `QRhiTexture::createFrom()`. **Do NOT use Qt Multimedia `QMediaPlayer`/`QVideoSink` for the live stream** (file-playback latency/buffering) — pick the single libavcodec pipeline.
  - **Correction (real zero-copy contract):** `createFrom()` is **not automatic** across devices. Either initialize FFmpeg's D3D11VA on the **same `ID3D11Device` as Qt's RHI**, or create the decode output texture **shared (`D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX`)** and synchronize with a keyed mutex. Same import contract for VAAPI→Vulkan/EGL (DMA-BUF, NV12 multi-plane, modifiers) and IOSurface→MTLTexture.
  - **Correction (latency vs vsync):** **decouple the live-video surface from hard local vsync** — present the latest decoded frame ASAP (mailbox/tear-allowed on the video region) and keep chrome vsync'd. Measure **glass-to-glass** latency, not just "vsync-correct." Evaluate an overlay/separate surface if one scene-graph schedule hurts the metric.
  - **Correction (QRhi stability):** `QRhi`/`QQuickRhiItem` are QPA-tier with **no source/binary-compat guarantee** across minor versions (require `Qt::GuiPrivate`). Isolate the present path behind your own interface; regression-test it on every Qt upgrade (especially 6.8→6.12).
  - Remove `QT_XCB_GL_INTEGRATION=xcb_egl` from the Wayland/VAAPI path (it is X11/XCB-only).
- **Qt licensing:** start under **LGPLv3** (Qt as shared libs, allow relink, ship notices). LGPL relink obligation is awkward for signed/notarized macOS bundles and any static/single-binary build — **budget a Qt commercial license** as a likely eventual cost.
- **Shared-memory ring:** only if a concrete local bulk payload exists (e.g. file-transfer chunks, clipboard images); with GPU-resident decode the video never crosses the IPC boundary as bytes, so do **not** include `QSharedMemory` speculatively.

### 3.6 Security

E2E via the **Noise Protocol Framework**: **Noise_XK** for first-contact (connecting side anonymous, host authenticated; **~1.5-RTT** to mutual auth) → **Noise_KK** when both static keys are pre-known (**1-RTT**). Drop the "0-RTT" framing. Run over **your own framing on UDP** (do not rely on TLS terminated at rendezvous).

1. **Identity:** per-device long-lived X25519 static keypair on first run, stored in the OS keystore (Keychain / DPAPI-CNG / libsecret) with a libsodium-encrypted file fallback. The **public key fingerprint (BLAKE2b "safety number") is the identity**; the REDesk ID is a routing handle only.
   - **macOS correction:** X25519/Ed25519 **cannot live in the Secure Enclave** (P-256 only). Store the libsodium X25519 key as an encrypted blob whose **wrapping key is a P-256 Secure Enclave key**.
2. **Zero-knowledge rendezvous/relay:** broker + blind-forward opaque Noise ciphertext only; never sees ephemeral/static private keys, session keys, or plaintext.
3. **Anti-MITM:** rendezvous **Ed25519-signs each peer's static pubkey** (verified before Noise — this is a **separate out-of-band layer, not part of Noise**, which is DH-only). Layer **TOFU + key-change alerts**, an **out-of-band fingerprint compare** UX, and **static-key pinning** for unattended fleets.
   - **Correction (precedent):** this is **better than** RustDesk — RustDesk's static-static `crypto_box` has **no forward secrecy**; Noise's ephemeral DH adds FS. Cite RustDesk only as the relay-only / signed-introducer precedent.
4. **Session authorization inside the Noise channel**, bound to the **handshake hash** (channel binding):
   - Interactive one-time PIN → **CPace (balanced PAKE)** — both sides hold a symmetric secret.
   - Unattended password → store **Argon2id** only and verify via **OPAQUE (augmented PAKE)**. Use the **OWASP minimum m=19456 KiB / t=2 / p=1** (or m=47104/t=1/p=1); reserve **RFC 9106's m=64 MiB/t=3/p=4** for the "stronger" tier with dedicated cores. Tune via `crypto_pwhash` to a ~0.5–1 s target rather than fixed constants.
5. **Replay protection:** Noise per-message monotonic nonces + AEAD (ChaCha20-Poly1305) reject in-session replay; handshake/auth carries a fresh server nonce + timestamp (reject stale/reused). Periodic Noise REKEY. **Application-layer framing/fragmentation above Noise** to respect the **65535-byte transport-message cap** for frames and file chunks.
6. **Permission model:** default-deny, least-privilege, explicit per-session capability grants (view-only vs control, clipboard, file transfer, audio, input injection, privacy/black-screen, UAC elevation — each a separate toggle). Persistent "being controlled" indicator + one-click disconnect/lock. Per-ID failed-auth rate-limit/lockout, peer-key allow/deny lists, audit log keyed by fingerprint.
- **Media plane is UDP-first** (DTLS 1.3 via wolfSSL/BoringSSL, or Noise-over-UDP) — **TCP relay only as a UDP-blocked fallback** (TCP HoL blocking is a primary smoothness killer).
- **PQ path:** hybrid **X25519 + ML-KEM-768** (FIPS 203). Isolate the KEM behind an interface (PQNoise tokens still pre-standard); budget ~1 KB+ handshake growth in the connection-setup latency budget.

---

## 4. Concrete Library List & License Flags

| Component | Library | License | Trap / Note |
|---|---|---|---|
| **Crypto core** | libsodium | ISC | Clean. Single crypto core (X25519, Ed25519, ChaCha20-Poly1305, BLAKE2b, Argon2id, secure mem). |
| Noise impl | noise-c (rweather) **or** hand-rolled on libsodium | MIT / — | Prefer noise-c or independently audit a custom state machine (nonce/channel-binding bugs). |
| PQ KEM | liboqs / ML-KEM-768 | MIT | Pre-standard PQNoise; isolate behind interface. |
| DTLS (media) | **wolfSSL** | **GPLv2 / commercial** | **Only wolfSSL ships DTLS 1.3** (since 5.4.0) — **buy commercial for closed-source**. |
| DTLS alt | OpenSSL 3.5 LTS | Apache-2.0 | **DTLS 1.2 only** (no DTLS 1.3 as of mid-2026). |
| DTLS alt | BoringSSL | ISC-ish permissive | If standard TLS/DTLS needed alongside WebRTC fallback. |
| **NAT/ICE** | libjuice | **MPL-2.0** | File-level copyleft — OK for proprietary linking; **keep ICE tuning in your own files**, don't edit libjuice sources (publication obligation). ~1.7.2 bundled. |
| WebRTC fallback | libdatachannel | **MPL-2.0** | Optional web-client only; no GCC/NACK/RTX. v0.24.5 (Jun 2026). |
| TURN relay | thin custom relay (primary) | your code | Default punch-fail path. |
| TURN relay | coturn / **eturnal** / Pion STUNner | BSD-3 / various | coturn operationally fragile — **eturnal/STUNner better operability**; standards TURN for web fallback only. |
| FEC | cm256 / leopard (Reed-Solomon) | BSD | Prefer over OpenFEC (**CeCILL/research** — avoid commercially). |
| Reliable ARQ | KCP (skywind3000) | MIT | Reference / input channel. |
| Bulk transfer (optional) | lsquic / quiche / msquic | MIT / BSD / MIT | **File-transfer channel only**, never interactive video. |
| **Video encode (HW)** | NVIDIA Video Codec SDK (NVENC/NVDEC) | proprietary, royalty-free to use | **Call directly** — routing via FFmpeg needs `--enable-nonfree` (un-redistributable). |
| | AMD AMF | MIT SDK | Call directly. |
| | Intel oneVPL / QSV | MIT | Call directly. |
| | Apple VideoToolbox | Apple SDK | macOS encode+decode; LTR-based loss recovery. |
| | Windows Media Foundation | Windows SDK | **Last-resort fallback only**, not an independent tier. |
| | VAAPI / libva | MIT | Linux Intel/AMD HW. |
| Video SW floor | x264 | **GPL-2.0+ / commercial** | High-profile SW H.264 — **GPL trap for closed-source; buy commercial**. |
| Video SW alt | openh264 | BSD-2-Clause | **Constrained Baseline only** — cannot emit High; Cisco royalty shelter covers **only Cisco's unmodified runtime-downloaded binary**, not a self-built copy. |
| FFmpeg (decode/glue) | libavcodec/libavutil | **LGPL-2.1+ / GPL** | **Build LGPL-only**; **avoid `--enable-gpl` and `--enable-nonfree`**. Decode/plumbing only; do not route HW encoders through it. |
| **Capture** | DXGI DDA, WGC | Windows SDK | Ships with Windows. |
| | ScreenCaptureKit | Apple SDK | macOS 12.3+; TCC re-grant trap. |
| | libpipewire + xdg-desktop-portal | MIT | Wayland. |
| | libX11/libXext/libXfixes (XShm) | MIT/X11 | X11 compatibility floor. |
| **Input** | Win32 SendInput / OpenInputDesktop | Windows SDK | uiAccess manifest + Authenticode signing required. |
| | CoreGraphics/Quartz Event Services | Apple SDK | Accessibility TCC. |
| | libei / liboeffis | MIT | Wayland injection; GNOME ≥46 / KDE ≥6.1. |
| | xdg-desktop-portal RemoteDesktop | portal spec | restore_token atomicity. |
| | wlroots virtual-pointer/keyboard | MIT/HPND | wlroots compositors. |
| | libXtst (XTEST) | MIT/X11 | Xorg. |
| **UI** | Qt 6.8 LTS (Quick/QML, Network, Core, GUI/RHI) | **LGPLv3 / commercial** | **LGPL relink obligation** painful for signed/notarized + static builds → likely commercial license. QRhi is QPA-tier (no BC guarantee). |
| Build | CMake ≥3.21 | BSD-3 | `qt_add_executable`/`qt_add_qml_module`. |

> **Patent-royalty traps (independent of GPL/LGPL):** **H.264/AVC** essential patents are active to ~Nov 2027 (Via LA 2026 restructure raised caps); **HEVC** has multiple aggressive pools (Access Advance / MPEG LA / Velos). Building FFmpeg LGPL-only **does not** clear these. **AV1 is royalty-safer.** Qt's LGPL/commercial license does **not** cover codec patents. **Get product counsel before shipping HW H.264/HEVC at scale.**

> **AGPL:** none in the stack — keep it that way. **GPL traps:** x264, wolfSSL(GPL build), FFmpeg `--enable-gpl`, OpenFEC(CeCILL). **MPL file-level copyleft:** libjuice, libdatachannel (fine if unmodified). **Un-redistributable:** any FFmpeg `--enable-nonfree` build.

---

## 5. Repo Structure (CMake)

See the live tree in this repo. Top-level layout:

```
REDesk/
├── CMakeLists.txt          # qt_standard_project_setup, options, version
├── cmake/                  # options, vendor SDK locators, Qt setup, signing, deploy
├── proto/                  # shared wire + IPC protocol definitions (one source of truth)
├── core/                   # platform-abstracted engine (NO OS-specific code)
│   ├── capture/  codec/  transport/  crypto/  input/  session/
├── platform/               # one backend dir per OS (only one builds)
│   ├── windows/  macos/  linux/
├── ui/                     # Qt Quick QML client (control panel + video item)
├── service/                # headless privileged daemon
├── server/                 # rendezvous/ + relay/
├── third_party/            # vendored / submodules (LICENSE audit per dir)
└── tests/                  # capability-probe matrix, transport sim, crypto vectors, fuzzers
```

---

## 6. Phased Roadmap

**Phase 0 — Foundations (de-risk the seams).** CMake skeleton, `core` interfaces, vendored deps + per-dir license audit. Spike the two hardest seams in isolation: (a) zero-copy capture→GPU-convert→encode→decode→`QQuickRhiItem` present on **each OS** (shared-device / keyed-mutex / DMA-BUF contracts); (b) Noise XK/KK handshake on libsodium with channel binding. Stand up the OS-version/HW test matrix.

**Phase 1 — LAN MVP (attended, direct only).** Single-monitor capture + H.264 HW encode/decode, GCC + pacer + keyframe governor, custom UDP **direct-only** (no NAT traversal yet), Noise E2E (XK), basic input injection (Windows SendInput / macOS CGEvent + TCC onboarding / Linux portal+libei), QML control panel + live video. Attended one-time PIN auth (CPace). Goal: smooth full-screen control across a LAN, glass-to-glass measured.

**Phase 2 — NAT traversal + relay.** libjuice ICE (host → STUN punch), rendezvous server (ID registry + ICE brokering + Ed25519 signing), thin blind-forward relay fallback, multi-region. TOFU + key-change alerts + fingerprint compare UX. Capacity-plan relay for 15–30%+ of sessions.

**Phase 3 — Codec & smoothness upgrades.** HEVC tier with 4:4:4 text, runtime AV1 probe, per-backend loss recovery (NVENC intra-refresh / VideoToolbox LTR), FEC + selective NACK tuning, adaptive overhead tied to GCC loss estimate. Multi-monitor. Decouple video surface from hard vsync; mailbox present.

**Phase 4 — Reliability & UX features.** Reliable file-transfer channel (optional QUIC/BBR), clipboard sync incl. virtual-file streaming, audio, full capability/permission model with toggles + "being controlled" indicator, per-ID rate-limit/lockout, audit log.

**Phase 5 — Unattended access & elevation.** Windows SYSTEM service + desktop-hop (UAC/lock/logon), Argon2id stored password + OPAQUE, peer-key pinning/allowlists, Linux restore_token persistence + GNOME/GDM scoping, macOS MDM/PPPC pre-approval + auto-login investigation (login-window control treated as a research item, not a guarantee). Key pinning for fleets.

**Phase 6 — Hardening & scale.** Independent security audit (Noise impl, IPC boundary, rendezvous zero-knowledge), hybrid PQ (X25519+ML-KEM-768), relay autoscaling/health/reselection, downgrade-attack protection, full per-OS QA matrix, optional standards/web-client path via libdatachannel.

---

## 7. Top Risks & Mitigations

1. **Custom congestion control + pacing (highest risk).** A naive GCC starves under competing TCP or bufferbloats. *Mitigation:* seed from a known-good GCC implementation; **mandatory packet pacer + keyframe-size governor**; extensive real-network testing; measure glass-to-glass, not just throughput.
2. **macOS TCC recurring re-grant + no pre-login capture.** Monthly/reboot re-confirmation is hostile to unattended use; SCK fails at the login window. *Mitigation:* MDM-delivered PPPC profiles, signed/notarized `.app`, `CGPreflightPostEventAccess` runtime checks, re-grant onboarding UX; treat login-screen control as a scoped research item.
3. **Zero-copy GPU seam fragility.** `createFrom()` is not automatic across devices; mismatched format/modifier silently falls to a CPU copy. *Mitigation:* same-device decode or shared-handle + keyed mutex; validate DMA-BUF modifiers; prototype per-OS in Phase 0; isolate behind a version-pinned interface (QRhi has no BC guarantee).
4. **Codec patent royalties (H.264/HEVC).** A larger, more certain cost than any library license. *Mitigation:* default to AV1 where HW-decode exists; legal sign-off before shipping HW H.264/HEVC at scale; keep AV1 as the royalty-safer upgrade.
5. **Licensing taint.** FFmpeg `--enable-nonfree` (un-redistributable), x264/wolfSSL GPL, MPL file-level copyleft. *Mitigation:* call vendor encode SDKs directly; LGPL-only FFmpeg; commercial wolfSSL/Qt/x264 where needed; keep libjuice tuning out of its sources; per-platform effective-license audit of the final binary.
6. **Relay cost & operational fragility.** Relayed sessions carry full video egress; expect 15–30%+ of sessions. *Mitigation:* thin custom relay as default, eturnal/STUNner over coturn for standards TURN, multi-region/anycast, health checks + graceful reselection, capacity planning.
7. **Owning the security stack.** Hand-rolled Noise, key management, IPC boundary, rendezvous brokering are an RCE/MITM surface. *Mitigation:* prefer vetted noise-c or independently audit; ACL-harden + authenticate the local IPC; rendezvous stays zero-knowledge; downgrade-attack pinning; Phase-6 external audit.
8. **Wayland / compositor fragmentation.** Portal+libei coverage uneven; per-version feature gaps; restore_token single-use rotation. *Mitigation:* runtime capability ladder + probe (never name/version inference), atomic token write-back, graceful "input unsupported / view-only" degraded mode, per-compositor test matrix.
9. **Windows Secure Desktop / uiAccess brittleness.** uiAccess needs valid signature + trusted path; Secure Desktop needs a SYSTEM service hopping desktops on WTS events — getting it wrong loses input exactly at a UAC prompt. *Mitigation:* enforce trusted-path install + signing checks; robust WTS session-change/desktop-switch handling with tests at the lock/UAC screen.
10. **Harvest-now-decrypt-later.** Pure X25519 sessions are future-quantum-readable. *Mitigation:* plan hybrid X25519+ML-KEM-768 now; isolate the KEM behind an interface; account for ~1 KB+ handshake growth in the setup-latency budget.
