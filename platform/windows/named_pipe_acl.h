#pragma once

// platform/windows/named_pipe_acl.h — security-descriptor helper for the
// service's local IPC named pipe (ADR-001 §3.5 IPC-hardening correction).
//
// The UI client is unprivileged and impersonable, so the service's named pipe
// must carry an EXPLICIT security descriptor: grant only the intended user
// SID(s) + Administrators + SYSTEM, and DENY Everyone/anonymous. Do NOT place
// the pipe in the per-session object namespace. The UI<->service channel is
// additionally authenticated at the message layer; every message is validated
// as untrusted (that authn lives in the service/IPC slice, not here).
//
// This header is dependency-free in the stub build (it only declares the
// helper); the real SECURITY_ATTRIBUTES construction is implemented behind
// REDESK_USE_REAL_BACKENDS in input_sendinput.cpp (co-located on Windows).

#include <string>

#include "core/common/types.h"

namespace redesk::platform::win {

// Allowed-principal policy for the service pipe.
struct PipeAclPolicy {
    bool allow_administrators = true;  // BUILTIN\Administrators
    bool allow_system = true;          // NT AUTHORITY\SYSTEM
    // Additional explicit user SIDs (string form, e.g. "S-1-5-21-...").
    // When empty, only Administrators/SYSTEM are granted.
    std::string extra_user_sid;
    // Hard requirement: Everyone (S-1-1-0) and anonymous are always DENIED.
};

// Opaque handle to a constructed SECURITY_ATTRIBUTES* (void* to keep this
// header free of <windows.h>). Lifetime owned by the caller; release with
// ReleaseServicePipeSecurity(). In the stub build this returns AccessLost-free
// Unsupported and yields nullptr.
struct PipeSecurity {
    void* security_attributes = nullptr;  // SECURITY_ATTRIBUTES*
    void* security_descriptor = nullptr;  // PSECURITY_DESCRIPTOR (for release)
};

// Build a hardened security descriptor for the service pipe per `policy`.
// TODO(ADR §3.5): construct a SDDL/explicit DACL granting only the policy
// principals (FILE_GENERIC_READ|WRITE), an explicit DENY ACE for S-1-1-0, set
// SE_DACL_PROTECTED so it does not inherit, and wrap in SECURITY_ATTRIBUTES.
redesk::Result<PipeSecurity> BuildServicePipeSecurity(const PipeAclPolicy& policy);

// Free the descriptor/attributes returned by BuildServicePipeSecurity().
void ReleaseServicePipeSecurity(PipeSecurity& sec);

} // namespace redesk::platform::win
