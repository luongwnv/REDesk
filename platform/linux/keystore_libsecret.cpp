// platform/linux/keystore_libsecret.cpp — Linux secret keystore backend.
// ADR-001 §3.6 (Security): per-device X25519 static key + unattended-password
// material stored via libsecret (Secret Service / freedesktop keyring), with a
// libsodium-encrypted file fallback when no keyring is available (headless).
//
// libsecret is LGPL (ADR §4) — linked as a shared lib, PRIVATE to the platform
// target. Crypto/Noise logic stays in core/crypto; this only protects bytes at
// rest.

#include "platform/platform_backends.h"

#if REDESK_USE_REAL_BACKENDS

#include "core/crypto/keystore.h"  // redesk::crypto::IKeyStore (core slice)

#include <libsecret/secret.h>

namespace redesk::platform {
namespace {

class LibSecretKeyStore final : public redesk::crypto::IKeyStore {
public:
    LibSecretKeyStore() = default;
    ~LibSecretKeyStore() override = default;

    redesk::Status Store(const std::string& key,
                         const std::vector<uint8_t>& secret) override {
        (void)key; (void)secret;
        // TODO(ADR §3.6): secret_password_store_sync with a REDesk schema
        // (attribute "key" = `key`), label "REDesk device key". Base64/hex the
        // raw bytes for the string-typed Secret Service value. If no Secret
        // Service is reachable (headless service), fall back to a libsodium
        // crypto_secretbox file under a machine-bound key. Zero plaintext.
        return Unsupported();
    }

    redesk::Result<std::vector<uint8_t>> Load(const std::string& key) override {
        (void)key;
        // TODO(ADR §3.6): secret_password_lookup_sync by attribute; decode back
        // to raw bytes; or read+decrypt the file fallback. Zero buffers.
        return redesk::Result<std::vector<uint8_t>>::fail(
            redesk::ErrorCode::NotFound, "libsecret Load not implemented");
    }

    redesk::Status Erase(const std::string& key) override {
        (void)key;
        // TODO(ADR §3.6): secret_password_clear_sync / unlink the fallback file.
        return Unsupported();
    }

private:
    static redesk::Status Unsupported() {
        return redesk::Status::error(redesk::ErrorCode::Unsupported,
                                     "libsecret keystore not implemented");
    }
};

} // namespace

std::unique_ptr<redesk::crypto::IKeyStore> CreateNativeKeyStore() {
    return std::make_unique<LibSecretKeyStore>();
}

} // namespace redesk::platform

#else // !REDESK_USE_REAL_BACKENDS

namespace redesk::platform {
std::unique_ptr<redesk::crypto::IKeyStore> CreateNativeKeyStore() {
    return nullptr;
}
} // namespace redesk::platform

#endif // REDESK_USE_REAL_BACKENDS
