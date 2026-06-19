// test_crypto_handshake — stub Noise sessions complete a handshake and
// encrypt/decrypt a round-trip, even with the INSECURE xor stub (ADR-001 §3.6).
//
// Asserts:
//   * an initiator + responder drive the toy XK-style handshake to mutual
//     established() by exchanging messages (mirrors the ~1.5-RTT XK lifecycle);
//   * both sides derive a matching channel-binding handshake hash (auth like
//     CPace/OPAQUE binds to this — ADR §3.6 #4);
//   * application data encrypted by one side decrypts to the same plaintext on
//     the other (the AEAD round-trip the transport relies on).

#include <vector>

#include "tests/redesk_test.h"
#include "tests/stub_backends.h"

using redesk::test::stub::INoiseSession;
using redesk::test::stub::makeStubInitiator;
using redesk::test::stub::makeStubResponder;

namespace {

// Pump the handshake to completion. Returns false if it doesn't converge in a
// bounded number of rounds (guards against a broken state machine hanging CI).
bool runHandshake(INoiseSession& initiator, INoiseSession& responder) {
    std::vector<uint8_t> in_to_resp;   // initiator -> responder
    std::vector<uint8_t> resp_to_in;   // responder -> initiator
    for (int round = 0; round < 8; ++round) {
        auto a = initiator.writeHandshake(resp_to_in);
        if (!a.ok()) return false;
        in_to_resp = a.value;
        resp_to_in.clear();

        auto b = responder.writeHandshake(in_to_resp);
        if (!b.ok()) return false;
        resp_to_in = b.value;

        if (initiator.established() && responder.established()) return true;
    }
    return initiator.established() && responder.established();
}

} // namespace

TEST(crypto, handshake_completes_and_binds) {
    auto initiator = makeStubInitiator();
    auto responder = makeStubResponder();
    ASSERT_TRUE(initiator != nullptr);
    ASSERT_TRUE(responder != nullptr);

    ASSERT_TRUE(runHandshake(*initiator, *responder));
    EXPECT_TRUE(initiator->established());
    EXPECT_TRUE(responder->established());

    // Channel binding (ADR §3.6 #4): both sides must agree on the handshake hash.
    auto hi = initiator->handshakeHash();
    auto hr = responder->handshakeHash();
    EXPECT_FALSE(hi.empty());
    EXPECT_TRUE(hi == hr);
}

TEST(crypto, aead_roundtrip_both_directions) {
    auto initiator = makeStubInitiator();
    auto responder = makeStubResponder();
    ASSERT_TRUE(runHandshake(*initiator, *responder));

    // Initiator -> responder.
    std::vector<uint8_t> msg1 = {'h', 'e', 'l', 'l', 'o'};
    auto ct1 = initiator->encrypt(msg1);
    EXPECT_TRUE(ct1 != msg1);  // not plaintext on the wire (even xor differs)
    auto pt1 = responder->decrypt(ct1);
    ASSERT_TRUE(pt1.ok());
    EXPECT_TRUE(pt1.value == msg1);

    // Responder -> initiator.
    std::vector<uint8_t> msg2 = {0x00, 0xFF, 0x10, 0x20};
    auto ct2 = responder->encrypt(msg2);
    auto pt2 = initiator->decrypt(ct2);
    ASSERT_TRUE(pt2.ok());
    EXPECT_TRUE(pt2.value == msg2);
}

TEST(crypto, distinct_nonces_change_ciphertext) {
    // Per-message nonce progression (replay protection groundwork, ADR §3.6 #5):
    // encrypting the same plaintext twice must not yield identical ciphertext.
    auto initiator = makeStubInitiator();
    auto responder = makeStubResponder();
    ASSERT_TRUE(runHandshake(*initiator, *responder));

    std::vector<uint8_t> msg = {1, 2, 3, 4};
    auto c1 = initiator->encrypt(msg);
    auto c2 = initiator->encrypt(msg);
    EXPECT_TRUE(c1 != c2);

    // Both still decrypt correctly.
    auto p1 = responder->decrypt(c1);
    auto p2 = responder->decrypt(c2);
    ASSERT_TRUE(p1.ok());
    ASSERT_TRUE(p2.ok());
    EXPECT_TRUE(p1.value == msg);
    EXPECT_TRUE(p2.value == msg);
}

REDESK_TEST_MAIN()
