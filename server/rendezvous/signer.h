#pragma once

// Ed25519 signed-introducer for the rendezvous server (ADR-001 §3.6.3).
//
// The rendezvous server Ed25519-signs each peer's X25519 static pubkey so the
// *other* peer can detect a MITM-substituted key BEFORE running the Noise
// handshake. This signature is a SEPARATE out-of-band trust layer — it is NOT
// part of Noise (which is DH-only). Clients pin the server's Ed25519 verify key
// out-of-band (shipped with the build / configured for a fleet) and layer
// TOFU + key-change alerts + fingerprint compare on top (§3.6.3).
//
// The server signs ONLY public material (the peer's static pubkey, optionally
// bound to the REDesk-ID + a freshness nonce). It has zero knowledge of media
// keys (§3.6.2). The Ed25519 *signing* key is the server's own long-lived key
// and is the only secret the rendezvous holds; it never touches peer secrets.
//
// Interface + two implementations:
//   - InsecureTestSigner (DEFAULT, stub build): deterministic FAKE signature,
//     clearly marked INSECURE-TEST-ONLY. Lets the protocol + client
//     verification path be exercised end-to-end with no crypto dependency.
//   - LibsodiumSigner (REDESK_USE_REAL_BACKENDS only): real Ed25519 via
//     libsodium (ISC, the single crypto core per §4). Gated so the stub build
//     stays dependency-free.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/common/types.h"
#include "server/rendezvous/protocol.h"

namespace redesk::server::rendezvous {

inline constexpr std::size_t kEd25519PubKeyLen = 32;
inline constexpr std::size_t kEd25519SigLen = 64;

// Abstract Ed25519 signer. One per server process (holds the server's signing
// key). Thread-safe in all provided impls.
class Signer {
public:
    virtual ~Signer() = default;

    // The server's Ed25519 verify (public) key. Clients pin this out-of-band.
    // 32 bytes for a real key; the stub returns a labeled placeholder.
    virtual const std::vector<std::uint8_t>& verify_key() const = 0;

    // Sign a peer's static pubkey, bound to its REDesk-ID so a signature for one
    // id cannot be replayed for another. The signed message is:
    //   "REDESK-INTRO-v1" || id || static_pubkey
    // Returns a 64-byte detached signature (real) or a deterministic fake (stub).
    virtual Result<std::vector<std::uint8_t>> sign_peer_key(
        const RedeskId& id, const StaticPubKey& static_pubkey) const = 0;

    // Whether this signer produces cryptographically real signatures. The
    // server logs a loud warning at startup when this is false.
    virtual bool is_secure() const = 0;
};

// Domain-separation prefix bound into every signed introduction (shared by the
// real signer and the client's verifier so they agree on the signed bytes).
inline constexpr char kIntroDomain[] = "REDESK-INTRO-v1";

// DEFAULT stub: deterministic FAKE signatures. INSECURE — TEST ONLY.
std::unique_ptr<Signer> make_insecure_test_signer();

#if defined(REDESK_USE_REAL_BACKENDS)
// Real Ed25519 signer (libsodium). `seed` is a 32-byte Ed25519 seed loaded from
// the OS keystore / config; if empty, a fresh keypair is generated (dev only —
// production must persist a stable key so clients' pins stay valid).
// TODO(ADR §3.6.1/§4): load the signing key from the OS keystore, not a flag.
Result<std::unique_ptr<Signer>> make_libsodium_signer(
    const std::vector<std::uint8_t>& seed);
#endif

} // namespace redesk::server::rendezvous
