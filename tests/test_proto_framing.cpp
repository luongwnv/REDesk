// test_proto_framing — the wire-framing contract in proto/transport.proto.h
// (ADR-001 §3.3 framing, §3.6 #5 above-Noise fragmentation).
//
// Asserts the header size is pinned, reliability policy matches the ADR, and the
// fragmentation math respects the 65535-byte Noise transport-message cap.

#include "proto/ipc.proto.h"
#include "proto/transport.proto.h"
#include "tests/redesk_test.h"

using namespace redesk::proto;

TEST(proto_framing, header_size_is_pinned) {
    // Mirrors the static_assert; a runtime check documents the wire contract.
    EXPECT_EQ(sizeof(FrameHeader), size_t{18});
    EXPECT_EQ(sizeof(IpcHeader), size_t{8});
}

TEST(proto_framing, reliability_policy_matches_adr) {
    // Video partially-reliable, audio unreliable, input/control/clipboard/file
    // reliable-ordered (ADR §3.3).
    EXPECT_TRUE(reliabilityFor(Channel::kVideo) == Reliability::kPartialReliable);
    EXPECT_TRUE(reliabilityFor(Channel::kAudio) == Reliability::kUnreliable);
    EXPECT_TRUE(reliabilityFor(Channel::kInput) == Reliability::kReliableOrdered);
    EXPECT_TRUE(reliabilityFor(Channel::kControl) == Reliability::kReliableOrdered);
    EXPECT_TRUE(reliabilityFor(Channel::kClipboard) == Reliability::kReliableOrdered);
    EXPECT_TRUE(reliabilityFor(Channel::kFile) == Reliability::kReliableOrdered);
}

TEST(proto_framing, whole_message_detection) {
    FrameHeader h;
    EXPECT_TRUE(isWholeMessage(h));  // default frag_count == 1

    h.frag_count = 3;
    h.flags = kFlagFragment;
    EXPECT_FALSE(isWholeMessage(h));
}

TEST(proto_framing, fragmentation_respects_noise_cap) {
    const uint32_t budget = usablePayloadBudget();  // <= 65535 - overhead
    EXPECT_TRUE(budget > 0);
    EXPECT_TRUE(budget < kNoiseMessageMax);  // header + AEAD tag consume some

    // A small message is a single frame.
    EXPECT_EQ(fragmentCount(100, budget), uint16_t{1});

    // A message exactly at the budget is one frame; one byte over is two.
    EXPECT_EQ(fragmentCount(budget, budget), uint16_t{1});
    EXPECT_EQ(fragmentCount(budget + 1, budget), uint16_t{2});

    // A large keyframe (e.g. 200 KB) splits into ceil(size/budget) frames, each
    // <= the Noise cap (ADR §3.6 #5).
    const uint32_t big = 200 * 1024;
    const uint16_t n = fragmentCount(big, budget);
    EXPECT_TRUE(n >= 4);
    EXPECT_TRUE(static_cast<uint32_t>(n) * budget >= big);          // covers it
    EXPECT_TRUE(static_cast<uint32_t>(n - 1) * budget < big);       // no waste
}

TEST(proto_framing, flag_helpers) {
    FrameHeader h;
    h.flags = kFlagKeyframe | kFlagFragment;
    EXPECT_TRUE(hasFlag(h, kFlagKeyframe));
    EXPECT_TRUE(hasFlag(h, kFlagFragment));
    EXPECT_FALSE(hasFlag(h, kFlagFec));
    EXPECT_FALSE(hasFlag(h, kFlagRetransmit));
}

REDESK_TEST_MAIN()
