# REDesk Protocol (`proto/`)

This directory is the **single source of truth** for REDesk's wire protocol (peer
↔ peer, over Noise/UDP) and its local IPC protocol (UI ↔ service). It maps
directly to **ADR-001 §3.3 (Transport), §3.5 (UI/IPC), §3.6 (Security)**.

## Why hand-rolled C++ headers (not `.proto`/gRPC)

The default scaffold build is `REDESK_USE_REAL_BACKENDS=OFF` and **must compile
with zero external dependencies** — no protobuf, no gRPC, no Qt. So the protocol
is expressed as plain, dependency-free C++ headers in `namespace redesk::proto`:

- `transport.proto.h` — channel multiplexing (`Channel`), the fixed-size
  `FrameHeader { channel, seq, flags, length, frag_index/count, type }`, message
  type enums for video/audio/input/control/clipboard/file, the per-channel
  reliability policy, and the **above-Noise fragmentation** helpers that respect
  the **65535-byte Noise transport-message cap** (ADR §3.6 #5).
- `codec.proto.h` — the ordered `CodecTier` ladder (H.264 floor → HEVC 4:2:0 →
  HEVC 4:4:4 → AV1) and `negotiateCodec()` (highest common tier). Codec
  selection is negotiated on the wire, so the tier enum is part of the contract.
- `ipc.proto.h` — the UI↔service RPC contract: `GetMyId`, `ConnectToPeer{id,pin}`,
  `SessionState`, `CapabilityGrant`, `InputEvent` passthrough, etc., as POD
  structs plus the `IpcMessageType` discriminated-union tag, and the `Capability`
  default-deny permission bits (ADR §3.6 #6).

## Status of these headers as a contract

These structs/enums **are the contract**. A future `.proto` schema (or a
`QDataStream`/protobuf codec) can be **generated from** them once a real
dependency budget exists; until then they are consumed verbatim by:

- `core/` — `core/transport` mirrors `Channel`/`FrameHeader`; `core/codec` uses
  `CodecTier`/`negotiateCodec`; `core/session` uses `Capability`.
- `service/` — implements the IPC server side of `ipc.proto.h`.
- `ui/` — implements the IPC client side of `ipc.proto.h`.
- `tests/` — exercises framing, negotiation, and IPC shapes directly.

## Wire-format rules (do not break silently)

- Enum integer values are **wire format**: append only, never renumber.
- `FrameHeader` / `IpcHeader` sizes are pinned by `static_assert`; changing them
  requires bumping `kProtocolVersion` and updating all peers.
- Multi-byte fields serialize **little-endian** (the in-memory structs are the
  mirror; the byte codec lives in `core/transport`).
- The `FrameHeader` rides **inside** the Noise ciphertext — relays see only
  opaque AEAD bytes (zero-knowledge relays, ADR §3.6 #2).

## CMake

`proto/CMakeLists.txt` defines a header-only `INTERFACE` target
`redesk_proto` (alias `REDesk::proto`) that propagates the repo-root include
path so consumers can `#include "proto/ipc.proto.h"`. It depends on
`REDesk::core` only for the shared value types in `core/common/types.h`.
