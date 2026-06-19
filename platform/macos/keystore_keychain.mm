// platform/macos/keystore_keychain.mm — macOS secret keystore backend.
// ADR-001 §3.6 (Security, macOS correction): X25519/Ed25519 CANNOT live in the
// Secure Enclave (P-256 only). Store the libsodium X25519 static key as an
// ENCRYPTED BLOB whose WRAPPING KEY is a P-256 Secure Enclave key; keep the
// wrapped blob (and unattended-password material) in the Keychain.
//
// Compiled only on (APPLE AND REDESK_USE_REAL_BACKENDS).

#include "platform/platform_backends.h"

#if REDESK_USE_REAL_BACKENDS

#include "core/crypto/keystore.h"  // redesk::crypto::IKeyStore (core slice)

#import <Security/Security.h>
#import <Foundation/Foundation.h>

namespace redesk::platform {
namespace {

class KeychainKeyStore final : public redesk::crypto::IKeyStore {
public:
    KeychainKeyStore() = default;
    ~KeychainKeyStore() override = default;

    redesk::Status Store(const std::string& key,
                         const std::vector<uint8_t>& secret) override {
        (void)key; (void)secret;
        // TODO(ADR §3.6): ensure a P-256 SecureEnclave wrapping key exists
        // (SecKeyCreateRandomKey with kSecAttrTokenIDSecureEnclave +
        // kSecAttrAccessControl, e.g. .privateKeyUsage / device-only). Wrap the
        // X25519 secret with SecKeyCreateEncryptedData (ECIES) and store the
        // ciphertext as a kSecClassGenericPassword item keyed by `key`,
        // kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly. Zero plaintext.
        return Unsupported();
    }

    redesk::Result<std::vector<uint8_t>> Load(const std::string& key) override {
        (void)key;
        // TODO(ADR §3.6): SecItemCopyMatching the wrapped blob, then
        // SecKeyCreateDecryptedData with the SEP private key to recover the
        // X25519 secret; zero the buffer after the caller consumes it.
        return redesk::Result<std::vector<uint8_t>>::fail(
            redesk::ErrorCode::NotFound, "Keychain Load not implemented");
    }

    redesk::Status Erase(const std::string& key) override {
        (void)key;
        // TODO(ADR §3.6): SecItemDelete the generic-password item.
        return Unsupported();
    }

private:
    static redesk::Status Unsupported() {
        return redesk::Status::error(redesk::ErrorCode::Unsupported,
                                     "Keychain keystore not implemented");
    }
};

} // namespace

std::unique_ptr<redesk::crypto::IKeyStore> CreateNativeKeyStore() {
    return std::make_unique<KeychainKeyStore>();
}

} // namespace redesk::platform

#endif // REDESK_USE_REAL_BACKENDS
