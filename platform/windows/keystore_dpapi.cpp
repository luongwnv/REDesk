// platform/windows/keystore_dpapi.cpp — Windows secret keystore backend.
// ADR-001 §3.6 (Security): per-device X25519 static key + unattended-password
// material stored at rest via Windows DPAPI. Prefer DPAPI-NG / CNG
// (NCryptProtectSecret) so the service (SYSTEM) and UI can share a protection
// scope; CryptProtectData (classic DPAPI) is the fallback.
//
// The fingerprint (BLAKE2b "safety number") is the identity; this keystore only
// protects the raw key bytes at rest — Noise/handshake logic lives in core/crypto.

#include "platform/platform_backends.h"

#if REDESK_USE_REAL_BACKENDS

#include "core/crypto/keystore.h"  // redesk::crypto::IKeyStore (core slice)

#include <windows.h>
#include <dpapi.h>   // CryptProtectData / CryptUnprotectData (fallback)
#include <ncrypt.h>  // NCryptProtectSecret / NCryptUnprotectSecret (preferred)

namespace redesk::platform {
namespace {

class DpapiKeyStore final : public redesk::crypto::IKeyStore {
public:
    DpapiKeyStore() = default;
    ~DpapiKeyStore() override = default;

    redesk::Status Store(const std::string& key,
                         const std::vector<uint8_t>& secret) override {
        (void)key; (void)secret;
        // TODO(ADR §3.6): NCryptProtectSecret with a LOCAL_MACHINE or
        // current-user descriptor (choose per service-vs-UI scope), persist the
        // ciphertext under `key` (registry/ProgramData). Fall back to
        // CryptProtectData with a machine entropy blob if CNG is unavailable.
        return Unsupported();
    }

    redesk::Result<std::vector<uint8_t>> Load(const std::string& key) override {
        (void)key;
        // TODO(ADR §3.6): read ciphertext, NCryptUnprotectSecret /
        // CryptUnprotectData; zero the plaintext buffer after use.
        return redesk::Result<std::vector<uint8_t>>::fail(
            redesk::ErrorCode::NotFound, "DPAPI Load not implemented");
    }

    redesk::Status Erase(const std::string& key) override {
        (void)key;
        return Unsupported();
    }

private:
    static redesk::Status Unsupported() {
        return redesk::Status::error(redesk::ErrorCode::Unsupported,
                                     "DPAPI keystore not implemented");
    }
};

} // namespace

std::unique_ptr<redesk::crypto::IKeyStore> CreateNativeKeyStore() {
    return std::make_unique<DpapiKeyStore>();
}

} // namespace redesk::platform

#else // !REDESK_USE_REAL_BACKENDS

namespace redesk::platform {
std::unique_ptr<redesk::crypto::IKeyStore> CreateNativeKeyStore() {
    return nullptr;
}
} // namespace redesk::platform

#endif // REDESK_USE_REAL_BACKENDS
