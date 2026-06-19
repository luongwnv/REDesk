// Ed25519 signer implementations (ADR-001 §3.6.3).
//
// The InsecureTestSigner is compiled in EVERY build so the protocol + client
// verification path work with no crypto dependency. The LibsodiumSigner is
// compiled only when REDESK_USE_REAL_BACKENDS is defined.

#include "server/rendezvous/signer.h"

#include <array>
#include <cstring>

#if defined(REDESK_USE_REAL_BACKENDS)
#  include <sodium.h>
#endif

namespace redesk::server::rendezvous {

namespace {

// Build the canonical signed message: domain || id || static_pubkey. Shared by
// real and stub paths and mirrored by the client verifier.
std::vector<std::uint8_t> intro_message(const RedeskId& id, const StaticPubKey& pk) {
    std::vector<std::uint8_t> m;
    const std::size_t domain_len = sizeof(kIntroDomain) - 1; // exclude NUL
    m.reserve(domain_len + id.size() + pk.size());
    m.insert(m.end(), kIntroDomain, kIntroDomain + domain_len);
    m.insert(m.end(), id.begin(), id.end());
    m.insert(m.end(), pk.begin(), pk.end());
    return m;
}

// ---------------------------------------------------------------------------
// INSECURE-TEST-ONLY signer. Deterministic, NON-cryptographic. Produces a
// 64-byte "signature" that is just a labeled, content-mixed digest so that the
// protocol round-trips and the verifier can be wired up — but it provides NO
// security whatsoever. Never enable in production.
// ---------------------------------------------------------------------------
class InsecureTestSigner final : public Signer {
public:
    InsecureTestSigner() {
        // A clearly-labeled fake verify key so anything inspecting it on the
        // wire can tell at a glance this is not a real key.
        static constexpr char kLabel[] = "INSECURE-TEST-ONLY-RENDEZVOUS-KY";
        verify_key_.assign(kLabel, kLabel + kEd25519PubKeyLen); // exactly 32 bytes
    }

    const std::vector<std::uint8_t>& verify_key() const override { return verify_key_; }

    Result<std::vector<std::uint8_t>> sign_peer_key(
        const RedeskId& id, const StaticPubKey& static_pubkey) const override {
        if (static_pubkey.size() != kStaticPubKeyLen) {
            return Result<std::vector<std::uint8_t>>::fail(
                ErrorCode::InvalidArgument, "static pubkey must be 32 bytes");
        }
        const auto msg = intro_message(id, static_pubkey);

        // Deterministic non-cryptographic mix (FNV-1a-ish per output byte). This
        // is INTENTIONALLY weak — it must never be mistaken for a MAC/signature.
        std::vector<std::uint8_t> sig(kEd25519SigLen, 0);
        std::uint64_t h = 1469598103934665603ull;
        for (std::size_t i = 0; i < msg.size(); ++i) {
            h ^= msg[i];
            h *= 1099511628211ull;
            sig[i % kEd25519SigLen] ^= static_cast<std::uint8_t>(h & 0xFF);
        }
        // Stamp a visible marker into the first bytes so logs/dumps scream FAKE.
        static constexpr char kMark[] = "FAKE";
        std::memcpy(sig.data(), kMark, 4);
        return Result<std::vector<std::uint8_t>>::good(std::move(sig));
    }

    bool is_secure() const override { return false; }

private:
    std::vector<std::uint8_t> verify_key_;
};

#if defined(REDESK_USE_REAL_BACKENDS)
// ---------------------------------------------------------------------------
// Real Ed25519 signer over libsodium (§4: single crypto core, ISC).
// ---------------------------------------------------------------------------
class LibsodiumSigner final : public Signer {
public:
    explicit LibsodiumSigner(const std::array<std::uint8_t, crypto_sign_PUBLICKEYBYTES>& pk,
                             const std::array<std::uint8_t, crypto_sign_SECRETKEYBYTES>& sk)
        : sk_(sk) {
        verify_key_.assign(pk.begin(), pk.end());
    }

    const std::vector<std::uint8_t>& verify_key() const override { return verify_key_; }

    Result<std::vector<std::uint8_t>> sign_peer_key(
        const RedeskId& id, const StaticPubKey& static_pubkey) const override {
        if (static_pubkey.size() != kStaticPubKeyLen) {
            return Result<std::vector<std::uint8_t>>::fail(
                ErrorCode::InvalidArgument, "static pubkey must be 32 bytes");
        }
        const auto msg = intro_message(id, static_pubkey);
        std::vector<std::uint8_t> sig(crypto_sign_BYTES, 0);
        unsigned long long sig_len = 0;
        if (crypto_sign_detached(sig.data(), &sig_len, msg.data(), msg.size(), sk_.data()) != 0) {
            return Result<std::vector<std::uint8_t>>::fail(
                ErrorCode::Internal, "crypto_sign_detached failed");
        }
        sig.resize(static_cast<std::size_t>(sig_len));
        return Result<std::vector<std::uint8_t>>::good(std::move(sig));
    }

    bool is_secure() const override { return true; }

private:
    std::vector<std::uint8_t> verify_key_;
    std::array<std::uint8_t, crypto_sign_SECRETKEYBYTES> sk_;
};
#endif // REDESK_USE_REAL_BACKENDS

} // namespace

std::unique_ptr<Signer> make_insecure_test_signer() {
    return std::make_unique<InsecureTestSigner>();
}

#if defined(REDESK_USE_REAL_BACKENDS)
Result<std::unique_ptr<Signer>> make_libsodium_signer(const std::vector<std::uint8_t>& seed) {
    if (sodium_init() < 0) {
        return Result<std::unique_ptr<Signer>>::fail(ErrorCode::Internal, "sodium_init failed");
    }
    std::array<std::uint8_t, crypto_sign_PUBLICKEYBYTES> pk{};
    std::array<std::uint8_t, crypto_sign_SECRETKEYBYTES> sk{};

    if (seed.empty()) {
        // Dev-only: ephemeral keypair. Production MUST pass a persisted seed so
        // client pins remain valid across restarts (TODO ADR §3.6.1).
        crypto_sign_keypair(pk.data(), sk.data());
    } else if (seed.size() == crypto_sign_SEEDBYTES) {
        crypto_sign_seed_keypair(pk.data(), sk.data(), seed.data());
    } else {
        return Result<std::unique_ptr<Signer>>::fail(
            ErrorCode::InvalidArgument,
            "Ed25519 seed must be " + std::to_string(crypto_sign_SEEDBYTES) + " bytes");
    }
    return Result<std::unique_ptr<Signer>>::good(
        std::make_unique<LibsodiumSigner>(pk, sk));
}
#endif

} // namespace redesk::server::rendezvous
