// test_session_permissions — default-deny capability gate denies an ungranted
// capability and allows a granted one; the audit log records decisions by
// fingerprint (ADR-001 §3.6 #6).

#include <string>

#include "proto/ipc.proto.h"
#include "tests/redesk_test.h"
#include "tests/stub_backends.h"

using redesk::proto::Capability;
using redesk::test::stub::PermissionGate;

namespace {
// BLAKE2b "safety number" stand-in — the fingerprint IS the identity (ADR §3.6 #1).
const std::string kPeerFp = "BLAKE2b:ab12-cd34-ef56-7890";
const std::string kOtherFp = "BLAKE2b:99zz-88yy-77xx-6666";
} // namespace

TEST(session_permissions, default_deny) {
    PermissionGate gate;
    // Nothing granted yet: every capability must be denied (default-deny).
    EXPECT_FALSE(gate.check(kPeerFp, Capability::kControlInput));
    EXPECT_FALSE(gate.check(kPeerFp, Capability::kClipboard));
    EXPECT_FALSE(gate.check(kPeerFp, Capability::kFileTransfer));
    EXPECT_FALSE(gate.check(kPeerFp, Capability::kUacElevation));
}

TEST(session_permissions, grant_allows_only_that_capability) {
    PermissionGate gate;
    gate.grant(kPeerFp, Capability::kControlInput);

    EXPECT_TRUE(gate.check(kPeerFp, Capability::kControlInput));  // granted
    // Other capabilities remain denied — least privilege (ADR §3.6 #6).
    EXPECT_FALSE(gate.check(kPeerFp, Capability::kClipboard));
    EXPECT_FALSE(gate.check(kPeerFp, Capability::kFileTransfer));
}

TEST(session_permissions, grants_are_per_fingerprint) {
    PermissionGate gate;
    gate.grant(kPeerFp, Capability::kViewScreen);
    // A different peer (different fingerprint) inherits nothing.
    EXPECT_TRUE(gate.check(kPeerFp, Capability::kViewScreen));
    EXPECT_FALSE(gate.check(kOtherFp, Capability::kViewScreen));
}

TEST(session_permissions, revoke_returns_to_deny) {
    PermissionGate gate;
    gate.grant(kPeerFp, Capability::kClipboard);
    EXPECT_TRUE(gate.check(kPeerFp, Capability::kClipboard));
    gate.revoke(kPeerFp, Capability::kClipboard);
    EXPECT_FALSE(gate.check(kPeerFp, Capability::kClipboard));
}

TEST(session_permissions, audit_log_records_by_fingerprint) {
    PermissionGate gate;
    gate.grant(kPeerFp, Capability::kControlInput);
    (void)gate.check(kPeerFp, Capability::kControlInput);   // allowed
    (void)gate.check(kPeerFp, Capability::kUacElevation);   // denied

    const auto& log = gate.audit();
    // grant + 2 checks recorded.
    ASSERT_TRUE(log.size() >= 3);

    // Every entry is keyed by the peer fingerprint (ADR §3.6 #6).
    for (const auto& e : log) {
        EXPECT_TRUE(e.fingerprint == kPeerFp);
    }

    // The allowed control-input check and the denied elevation check are both
    // present with the correct decision.
    bool saw_allowed_control = false;
    bool saw_denied_elevation = false;
    for (const auto& e : log) {
        if (e.action == "check" && e.capability == Capability::kControlInput &&
            e.allowed) {
            saw_allowed_control = true;
        }
        if (e.action == "check" && e.capability == Capability::kUacElevation &&
            !e.allowed) {
            saw_denied_elevation = true;
        }
    }
    EXPECT_TRUE(saw_allowed_control);
    EXPECT_TRUE(saw_denied_elevation);
}

REDESK_TEST_MAIN()
