#pragma once

// End-to-end crypto abstraction (ADR-001 §3.6).
//
// E2E via the Noise Protocol Framework: Noise_XK for first contact (connecting
// side anonymous, host authenticated, ~1.5-RTT) -> Noise_KK when both static
// keys are pre-known (1-RTT). libsodium is the single crypto core in the real
// backend (X25519, ChaCha20-Poly1305, BLAKE2b). This header only exposes the
// state-machine SURFACE; the cipher choice lives behind the implementation.
//
// Identity: a per-device long-lived X25519 static keypair. The BLAKE2b
// fingerprint ("safety number") IS the identity; the REDesk ID is a routing
// handle only (§3.6.1). The fingerprint is rendered as a human-comparable
// safety-number string for the out-of-band verification UX (§3.6.3).
//
// IMPORTANT (§3.6.3): the rendezvous Ed25519-signs each peer's static pubkey as
// a SEPARATE out-of-band anti-MITM layer — that is NOT part of Noise (Noise is
// DH-only). That signing/verification lives in server/ + session/, not here.
//
// The default STUB (REDESK_USE_REAL_BACKENDS=OFF) is INSECURE BY DESIGN: a
// trivial handshake and an XOR "cipher", clearly marked, so transport/session
// tests can exercise the encrypt/decrypt + handshake flow with zero crypto deps.
// It MUST NEVER be compiled into a shipping build.

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/common/types.h"

namespace redesk::crypto {

// X25519 public/secret key sizes (libsodium crypto_scalarmult_curve25519).
inline constexpr size_t kX25519PublicKeyLen = 32;
inline constexpr size_t kX25519SecretKeyLen = 32;

using PublicKey = std::array<uint8_t, kX25519PublicKeyLen>;
using SecretKey = std::array<uint8_t, kX25519SecretKeyLen>;

// A device's long-lived static identity. The secret half is present only for the
// local device; a peer's Identity carries the public key + fingerprint only.
struct Identity {
    PublicKey public_key{};
    SecretKey secret_key{};        // all-zero for remote peers
    bool has_secret = false;

    // BLAKE2b "safety number" over the public key, rendered as grouped decimal
    // digits for the out-of-band compare UX (Signal-style). Stable per key.
    std::string fingerprint() const;
};

// Which Noise handshake pattern to run (§3.6).
enum class NoisePattern {
    XK,   // first contact: initiator anonymous, responder static known
    KK,   // both static keys pre-known (pinned/unattended fleets)
};

enum class HandshakeRole {
    Initiator,
    Responder,
};

// Noise session state-machine surface. Drive the handshake by alternately
// calling writeMessage()/readMessage() until isHandshakeComplete(); then use
// encrypt()/decrypt() for transport messages (each capped at 65535 bytes — the
// caller fragments above this, §3.6.5).
class INoiseSession {
public:
    virtual ~INoiseSession() = default;

    // Produce the next handshake message to send. `payload` may carry early data
    // where the pattern allows; output goes into `out`.
    virtual Status writeMessage(const std::vector<uint8_t>& payload,
                                std::vector<uint8_t>& out) = 0;

    // Consume a received handshake message; any decrypted payload goes to `out`.
    virtual Status readMessage(const std::vector<uint8_t>& message,
                               std::vector<uint8_t>& out) = 0;

    virtual bool isHandshakeComplete() const = 0;

    // Channel-binding handle: the Noise handshake hash, to which session
    // authorization (CPace/OPAQUE PINs) is bound (§3.6.4). Valid only after the
    // handshake completes.
    virtual std::vector<uint8_t> handshakeHash() const = 0;

    // The remote peer's static public key, learned during the handshake (XK) or
    // supplied up front (KK). Empty until known.
    virtual PublicKey remoteStaticKey() const = 0;

    // Transport-phase AEAD. Each call advances the per-direction nonce
    // (monotonic, replay-rejecting, §3.6.5). `ad` is associated data.
    virtual Status encrypt(const std::vector<uint8_t>& plaintext,
                           const std::vector<uint8_t>& ad,
                           std::vector<uint8_t>& ciphertext) = 0;
    virtual Status decrypt(const std::vector<uint8_t>& ciphertext,
                           const std::vector<uint8_t>& ad,
                           std::vector<uint8_t>& plaintext) = 0;

    // Periodic forward-secrecy rekey (§3.6.5). No-op in the stub.
    virtual void rekey() = 0;
};

// Device static-key storage (§3.6.1). Real backend: OS keystore (Keychain /
// DPAPI-CNG / libsecret) with a libsodium-encrypted file fallback; on macOS the
// X25519 blob is wrapped by a P-256 Secure Enclave key. Stub is in-memory only.
class IKeyStore {
public:
    virtual ~IKeyStore() = default;

    // Load the existing device identity, if any (includes the secret key).
    virtual Result<Identity> load() = 0;

    // Generate a fresh device identity and persist it (replacing any existing).
    virtual Result<Identity> generate() = 0;

    // Persist an externally-created identity (e.g. imported / rotated).
    virtual Status store(const Identity& identity) = 0;

    // Convenience: load() if present, else generate(). The normal first-run path.
    virtual Result<Identity> loadOrGenerate() = 0;
};

// Factories. Real backend (libsodium/Noise + OS keystore) when
// REDESK_USE_REAL_BACKENDS=ON, else the INSECURE-FOR-TESTS-ONLY stub.
std::unique_ptr<INoiseSession> createNoiseSession(NoisePattern pattern,
                                                  HandshakeRole role,
                                                  const Identity& local_static,
                                                  const PublicKey& remote_static);

std::unique_ptr<IKeyStore> createKeyStore();

} // namespace redesk::crypto
