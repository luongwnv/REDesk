// INSECURE crypto stub (ADR-001 §3.6) — compiled by default
// (REDESK_USE_REAL_BACKENDS=OFF). EVERYTHING here is fake:
//
//   * "Handshake": a 2-message exchange of static public keys with NO Diffie-
//     Hellman, NO authentication, NO confidentiality.
//   * "Cipher": a repeating-key XOR keyed off the concatenated static keys.
//     This is NOT encryption. It exists only so the transport/session layers can
//     run encrypt()/decrypt() round-trips in unit tests.
//   * KeyStore: in-memory; keys vanish on process exit.
//   * Fingerprint: a non-cryptographic FNV-1a hash rendered as a safety number,
//     standing in for BLAKE2b purely for stable, comparable test output.
//
//  >>> THIS FILE MUST NEVER BE LINKED INTO A SHIPPING BUILD. <<<
//
// TODO(ADR §3.6): the real backend uses libsodium (X25519, ChaCha20-Poly1305,
// BLAKE2b, Argon2id) with a real Noise_XK/KK state machine (noise-c or an
// independently-audited hand-roll), monotonic replay-rejecting nonces, channel
// binding via the handshake hash, periodic REKEY, and OS-keystore identity
// storage (Keychain/DPAPI-CNG/libsecret; macOS P-256-Secure-Enclave wrap).

#include "core/crypto/crypto.h"

#include <cstdio>

#include "core/common/logging.h"

namespace redesk::crypto {

namespace {

// Non-cryptographic 64-bit FNV-1a — placeholder for BLAKE2b in the stub only.
uint64_t fnv1a(const uint8_t* data, size_t len) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < len; ++i) {
        h ^= data[i];
        h *= 1099511628211ull;
    }
    return h;
}

} // namespace

std::string Identity::fingerprint() const {
    // Render as five space-separated 5-digit groups (Signal-style safety number).
    // Real impl: BLAKE2b(public_key) -> grouped digits. Here: FNV-1a, clearly
    // weaker, but stable and comparable for the out-of-band-compare UX tests.
    const uint64_t h = fnv1a(public_key.data(), public_key.size());
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%05u %05u %05u %05u %05u",
                  static_cast<unsigned>((h) % 100000),
                  static_cast<unsigned>((h / 100000) % 100000),
                  static_cast<unsigned>((h / 10000000000ull) % 100000),
                  static_cast<unsigned>((h / 1000000000000000ull) % 100000),
                  static_cast<unsigned>((h >> 1) % 100000));
    return std::string(buf);
}

namespace {

// Deterministic pseudo-key generator for the stub keystore (NOT random, NOT a
// real keypair — secret == public-ish for test stability). Seeded by a counter
// so successive generate() calls differ.
PublicKey derivePublic(const SecretKey& sk) {
    PublicKey pk{};
    // Fake "scalarmult": reverse + xor a constant. Anything stable + reversible.
    for (size_t i = 0; i < pk.size(); ++i) {
        pk[i] = static_cast<uint8_t>(sk[sk.size() - 1 - i] ^ 0x5A);
    }
    return pk;
}

Identity makeStubIdentity(uint8_t seed) {
    Identity id;
    for (size_t i = 0; i < id.secret_key.size(); ++i) {
        id.secret_key[i] = static_cast<uint8_t>(seed + i * 7 + 1);
    }
    id.public_key = derivePublic(id.secret_key);
    id.has_secret = true;
    return id;
}

// ---------------------------------------------------------------------------
// INSECURE XOR "Noise" session.
// ---------------------------------------------------------------------------
class StubNoiseSession final : public INoiseSession {
public:
    StubNoiseSession(NoisePattern pattern, HandshakeRole role,
                     Identity local_static, PublicKey remote_static)
        : pattern_(pattern),
          role_(role),
          local_(std::move(local_static)),
          remote_static_(remote_static) {
        // KK assumes the remote static is already known; XK learns it from the
        // responder's handshake message.
        remote_known_ = (pattern_ == NoisePattern::KK);
        // role_ is recorded for the real Noise_XK/KK state machine (initiator vs
        // responder drive different message sequences); the stub is symmetric.
        (void)role_;
    }

    Status writeMessage(const std::vector<uint8_t>& payload,
                        std::vector<uint8_t>& out) override {
        if (handshake_done_) {
            return Status::error(ErrorCode::InvalidArgument,
                                 "handshake already complete");
        }
        // Each side sends its static public key once, then early payload.
        out.clear();
        out.insert(out.end(), local_.public_key.begin(), local_.public_key.end());
        out.insert(out.end(), payload.begin(), payload.end());
        ++messages_written_;
        maybeComplete();
        return Status::success();
    }

    Status readMessage(const std::vector<uint8_t>& message,
                       std::vector<uint8_t>& out) override {
        if (handshake_done_) {
            return Status::error(ErrorCode::InvalidArgument,
                                 "handshake already complete");
        }
        if (message.size() < kX25519PublicKeyLen) {
            return Status::error(ErrorCode::InvalidArgument,
                                 "short handshake message");
        }
        // Learn the peer's static key (XK first-contact).
        for (size_t i = 0; i < kX25519PublicKeyLen; ++i) {
            remote_static_[i] = message[i];
        }
        remote_known_ = true;
        out.assign(message.begin() + kX25519PublicKeyLen, message.end());
        ++messages_read_;
        maybeComplete();
        return Status::success();
    }

    bool isHandshakeComplete() const override { return handshake_done_; }

    std::vector<uint8_t> handshakeHash() const override {
        // Fake channel binding: hash of both static keys. Real impl uses the
        // running Noise handshake hash `h`.
        std::vector<uint8_t> buf;
        buf.insert(buf.end(), local_.public_key.begin(), local_.public_key.end());
        buf.insert(buf.end(), remote_static_.begin(), remote_static_.end());
        const uint64_t h = fnv1a(buf.data(), buf.size());
        std::vector<uint8_t> out(8);
        for (int i = 0; i < 8; ++i) {
            out[i] = static_cast<uint8_t>((h >> (i * 8)) & 0xFF);
        }
        return out;
    }

    PublicKey remoteStaticKey() const override { return remote_static_; }

    Status encrypt(const std::vector<uint8_t>& plaintext,
                   const std::vector<uint8_t>& ad,
                   std::vector<uint8_t>& ciphertext) override {
        if (!handshake_done_) {
            return Status::error(ErrorCode::InvalidArgument,
                                 "handshake not complete");
        }
        if (plaintext.size() > kMaxMessage) {
            return Status::error(ErrorCode::InvalidArgument,
                                 "plaintext exceeds 65535-byte Noise cap");
        }
        // INSECURE: XOR with the key stream + nonce; append the nonce so the peer
        // can reverse it. No integrity, no confidentiality. Tests only.
        (void)ad;
        const uint64_t nonce = send_nonce_++;
        ciphertext.clear();
        ciphertext.reserve(plaintext.size() + sizeof(nonce));
        for (int i = 0; i < 8; ++i) {
            ciphertext.push_back(static_cast<uint8_t>((nonce >> (i * 8)) & 0xFF));
        }
        xorStream(plaintext, nonce, ciphertext);
        return Status::success();
    }

    Status decrypt(const std::vector<uint8_t>& ciphertext,
                   const std::vector<uint8_t>& ad,
                   std::vector<uint8_t>& plaintext) override {
        if (!handshake_done_) {
            return Status::error(ErrorCode::InvalidArgument,
                                 "handshake not complete");
        }
        if (ciphertext.size() < sizeof(uint64_t)) {
            return Status::error(ErrorCode::InvalidArgument, "short ciphertext");
        }
        (void)ad;
        uint64_t nonce = 0;
        for (int i = 0; i < 8; ++i) {
            nonce |= static_cast<uint64_t>(ciphertext[i]) << (i * 8);
        }
        // Replay rejection: nonce must be strictly increasing per direction.
        if (nonce < recv_nonce_) {
            return Status::error(ErrorCode::PermissionDenied,
                                 "replayed/old nonce");
        }
        recv_nonce_ = nonce + 1;
        std::vector<uint8_t> body(ciphertext.begin() + sizeof(uint64_t),
                                  ciphertext.end());
        plaintext.clear();
        xorStream(body, nonce, plaintext);
        return Status::success();
    }

    void rekey() override { /* no-op in stub */ }

private:
    void maybeComplete() {
        // XK/KK both reach "done" in this stub once each side has written and
        // read at least one message and the remote static is known.
        if (remote_known_ && messages_written_ >= 1 && messages_read_ >= 1) {
            if (!handshake_done_) {
                handshake_done_ = true;
                REDESK_LOG(Warn, "crypto")
                    << "INSECURE stub handshake complete (XOR cipher) — "
                       "tests only, never ship";
            }
        }
    }

    void xorStream(const std::vector<uint8_t>& in, uint64_t nonce,
                   std::vector<uint8_t>& out) const {
        // Key stream = repeating (local||remote static) keyed bytes mixed with
        // the nonce. Purely to make round-trips reversible.
        std::vector<uint8_t> key;
        key.insert(key.end(), local_.public_key.begin(), local_.public_key.end());
        key.insert(key.end(), remote_static_.begin(), remote_static_.end());
        for (size_t i = 0; i < in.size(); ++i) {
            const uint8_t k = static_cast<uint8_t>(
                key[i % key.size()] ^ static_cast<uint8_t>((nonce + i) & 0xFF));
            out.push_back(static_cast<uint8_t>(in[i] ^ k));
        }
    }

    static constexpr size_t kMaxMessage = 65535;

    NoisePattern pattern_;
    HandshakeRole role_;
    Identity local_;
    PublicKey remote_static_{};
    bool remote_known_ = false;
    bool handshake_done_ = false;
    int messages_written_ = 0;
    int messages_read_ = 0;
    uint64_t send_nonce_ = 0;
    uint64_t recv_nonce_ = 0;
};

// ---------------------------------------------------------------------------
// In-memory keystore.
// ---------------------------------------------------------------------------
class StubKeyStore final : public IKeyStore {
public:
    Result<Identity> load() override {
        if (!have_) {
            return Result<Identity>::fail(ErrorCode::NotFound,
                                          "no stored identity");
        }
        return Result<Identity>::good(identity_);
    }

    Result<Identity> generate() override {
        identity_ = makeStubIdentity(seed_++);
        have_ = true;
        REDESK_LOG(Info, "crypto")
            << "generated stub device identity, fingerprint "
            << identity_.fingerprint();
        return Result<Identity>::good(identity_);
    }

    Status store(const Identity& identity) override {
        identity_ = identity;
        have_ = true;
        return Status::success();
    }

    Result<Identity> loadOrGenerate() override {
        if (have_) {
            return Result<Identity>::good(identity_);
        }
        return generate();
    }

private:
    Identity identity_;
    bool have_ = false;
    uint8_t seed_ = 1;
};

} // namespace

std::unique_ptr<INoiseSession> createNoiseSession(NoisePattern pattern,
                                                  HandshakeRole role,
                                                  const Identity& local_static,
                                                  const PublicKey& remote_static) {
#if defined(REDESK_USE_REAL_BACKENDS)
    // TODO(ADR §3.6): construct the libsodium-backed Noise_XK/KK session here
    // from (pattern, role, local_static, remote_static).
    (void)pattern;
    (void)role;
    (void)local_static;
    (void)remote_static;
    REDESK_LOG(Error, "crypto")
        << "real Noise session not implemented; refusing to fall back to the "
           "INSECURE stub in a real-backends build";
    return nullptr;
#else
    return std::make_unique<StubNoiseSession>(pattern, role, local_static,
                                              remote_static);
#endif
}

std::unique_ptr<IKeyStore> createKeyStore() {
#if defined(REDESK_USE_REAL_BACKENDS)
    // TODO(ADR §3.6.1): OS keystore (Keychain/DPAPI-CNG/libsecret) backend.
    return std::make_unique<StubKeyStore>();
#else
    return std::make_unique<StubKeyStore>();
#endif
}

} // namespace redesk::crypto
