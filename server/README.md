# REDesk Servers

Two small, horizontally-scalable, multi-region server roles. They map 1:1 onto
RustDesk's `hbbs`/`hbbr` split, but the rendezvous is a **signed-introducer with
zero knowledge of media keys** (a deliberate upgrade over RustDesk — see below).
This directory is the source of truth for the scaffold described in
[`docs/adr/ADR-001-architecture.md`](../docs/adr/ADR-001-architecture.md)
§2, §3.3, and §3.6.

```
   ┌──────────────┐      (1) register / lookup / broker      ┌──────────────┐
   │   PEER  A    │◄────────────────┐         ┌─────────────►│   PEER  B    │
   │ (controller) │     control     │         │   control    │   (host)     │
   └──────┬───────┘                 ▼         ▼              └──────┬───────┘
          │              ┌────────────────────────────┐            │
          │              │   redesk-rendezvous (hbbs)  │            │
          │              │   • ID → endpoint registry  │            │
          │              │   • brokers ICE/SDP         │            │
          │              │   • Ed25519-signs pubkeys   │            │
          │              │   • ZERO media-key knowledge │           │
          │              └────────────────────────────┘            │
          │                                                         │
          │   (2) ICE hole-punch ── DIRECT P2P (preferred) ─────────┤
          │        Noise-encrypted UDP, no server in the path        │
          │                                                         │
          │   (3) punch-fail fallback (~15–30%+ of sessions)        │
          │              ┌────────────────────────────┐            │
          └─────────────►│    redesk-relay (hbbr)      │◄───────────┘
                         │  • blind-forward ciphertext │
                         │  • keyed by session token   │
                         │  • NEVER decrypts            │
                         │  • multi-region / anycast    │
                         └────────────────────────────┘
```

## Topology

### `redesk-rendezvous` — signaling / zero-knowledge (≈ RustDesk `hbbs`)

The control plane. It does three things and only three things:

1. **ID → endpoint registry.** Maps a REDesk-ID (a *routing handle only*) to the
   peer's last-known reachable endpoint, X25519 static pubkey, and last-seen
   time. The cryptographic identity is the BLAKE2b fingerprint of the static
   pubkey (ADR §3.6.1), **not** the REDesk-ID.
2. **ICE candidate / SDP broker.** Relays opaque ICE/SDP blobs between two peers
   by a short-lived session token so they can attempt a direct hole-punch. The
   server treats the blobs as opaque and only routes them.
3. **Ed25519 signed introducer.** Signs each peer's static pubkey so the other
   side can detect a MITM-substituted key **before** the Noise handshake. This
   signature is a **separate out-of-band trust layer — it is NOT part of Noise**
   (Noise is DH-only). Clients pin the server's Ed25519 verify key out-of-band
   and layer TOFU + key-change alerts + fingerprint compare on top (ADR §3.6.3).

The rendezvous has **zero knowledge of media keys** (ADR §3.6.2): it never sees
ephemeral/static private keys, Noise session keys, or plaintext. The only secret
it holds is its own Ed25519 *signing* key, and it only ever signs **public**
material.

> **Why this is better than RustDesk.** RustDesk's static-static `crypto_box`
> design has **no forward secrecy**. REDesk runs Noise (XK → KK) end-to-end, so
> the rendezvous is a *signed introducer* over Noise's ephemeral DH — it never
> needs to know any key that could decrypt a session. We cite RustDesk only as
> the relay-only / signed-introducer **topology** precedent, not its crypto.

Control protocol (`protocol.h`): `REGISTER`, `KEEPALIVE`, `LOOKUP`,
`BROKER_CANDIDATES` (+ server-origin `REGISTER_OK` / `LOOKUP_OK` /
`LOOKUP_FAIL`). UDP carries keepalive/punch coordination; a TCP/TLS-443 path
exists for UDP-blocked networks (ADR §3.3).

### `redesk-relay` — thin blind-forward fallback (≈ RustDesk `hbbr`)

The media-plane fallback used when ICE hole-punching fails (CGNAT, symmetric
NAT, locked-down enterprise networks). **Plan for 15–30%+ of sessions to relay**
— not the optimistic ~3% (ADR §3.3, Risk #6).

It forwards **opaque Noise ciphertext** between the two peers' endpoints, keyed
by a session/allocation token (`allocation_table.h`), and **never decrypts**.
It learns each peer's source endpoint on first contact (address learning,
symmetric to ICE discovery), pairs the two legs, and shuttles datagrams
verbatim.

> **`redesk-relay` vs coturn/eturnal.** This proprietary relay is the **default**
> punch-fail path: dumber, cheaper, and tuned for our own transport. Standards
> **TURN** (coturn / **eturnal** / Pion STUNner) is reserved **only** for the
> web-client / `libdatachannel` fallback path (ADR §3.3, §4). `eturnal`/STUNner
> are preferred over coturn for operability if/when standards TURN is needed.

## Mapping to RustDesk

| REDesk            | RustDesk | Role                                              |
|-------------------|----------|---------------------------------------------------|
| `redesk-rendezvous` | `hbbs`  | ID registry + signaling/brokering                 |
| `redesk-relay`      | `hbbr`  | blind-forward relay for punch-fail sessions       |

Key difference: REDesk's rendezvous is **zero-knowledge + Ed25519 signed
introducer**; the relay forwards **Noise** ciphertext (forward-secret), not
RustDesk's `crypto_box` secretbox stream.

## Build

Both binaries build in the **default stub build** with **no external
dependencies** (C++20 standard library + a portable BSD-socket wrapper in
`server/common/`):

```sh
cmake -B build -DREDESK_USE_REAL_BACKENDS=OFF
cmake --build build --target redesk-rendezvous redesk-relay
```

In the stub build the rendezvous uses an **`InsecureTestSigner`** that emits a
deterministic **FAKE** signature (marked `INSECURE-TEST-ONLY`) and prints a loud
startup warning — it exists only so the protocol and the client's verification
path can be exercised end-to-end without a crypto dependency. The real Ed25519
signer (libsodium, ISC — the single crypto core per ADR §4) compiles in only
under `-DREDESK_USE_REAL_BACKENDS=ON`.

```sh
./build/bin/redesk-rendezvous --port 21116
./build/bin/redesk-relay      --port 21117
```

## Deployment notes

- **Horizontal scale / multi-region / anycast.** Both roles are stateless enough
  to run as a fleet behind anycast. The in-memory registry/allocation tables in
  this scaffold are single-node stubs; production swaps in a sharded/replicated
  store (registry) and consistent token routing (relay) with health checks +
  graceful reselection (ADR §3.3, Risk #6). See the `TODO(ADR §3.3)` markers in
  `registry_store.cpp` and `allocation_table.cpp`.
- **Relay capacity = real cost.** Relayed sessions carry full video egress.
  Capacity-plan for 15–30%+ of sessions and place relays close to users.
- **Ed25519 signing key must be persisted.** Clients pin the rendezvous verify
  key; a fresh key on every restart would invalidate every pin. Load a stable
  seed from the OS keystore / config (`TODO(ADR §3.6.1)` in `signer.cpp`).
- **Don't terminate TLS at the rendezvous for security.** End-to-end security is
  Noise over UDP (ADR §3.6). TLS-443 is only a transport for UDP-blocked
  control traffic, not a trust boundary.
- **Abuse guards.** The relay should only forward for **authenticated
  allocations** (an `ALLOCATE` verb requested via the rendezvous) so it is not an
  open reflector; the stub caps each token to two legs + a TTL as a coarse
  placeholder. Add per-ID failed-auth rate-limiting/lockout on the rendezvous
  (ADR §3.6.6).

## File map

| File                              | Purpose                                              |
|-----------------------------------|------------------------------------------------------|
| `common/net.{h,cpp}`              | portable BSD-socket wrapper (UDP/TCP, win/posix)     |
| `rendezvous/protocol.{h,cpp}`     | control message types + skeleton framing             |
| `rendezvous/registry_store.{h,cpp}` | ID→{endpoint,pubkey,last_seen} registry (stub)     |
| `rendezvous/signer.{h,cpp}`       | Ed25519 signer (insecure stub + libsodium real)      |
| `rendezvous/main.cpp`             | `redesk-rendezvous` control listener skeleton        |
| `relay/allocation_table.{h,cpp}`  | token → two-leg session table (stub)                 |
| `relay/main.cpp`                  | `redesk-relay` blind-forward UDP loop skeleton       |
