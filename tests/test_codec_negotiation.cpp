// test_codec_negotiation — negotiateCodec() picks the highest common tier
// (ADR-001 §3.2 tiered, bidirectional negotiation). Depends only on the proto
// contract (proto/codec.proto.h), which is the wire source of truth.

#include "proto/codec.proto.h"
#include "tests/redesk_test.h"

using redesk::proto::CodecTier;
using redesk::proto::floorCapabilities;
using redesk::proto::negotiateCodec;

TEST(codec_negotiation, picks_highest_common_tier) {
    // Local supports the full ladder; remote tops out at HEVC 4:2:0.
    std::vector<CodecTier> local = {CodecTier::kH264High420, CodecTier::kHevc420,
                                    CodecTier::kHevc444, CodecTier::kAv1};
    std::vector<CodecTier> remote = {CodecTier::kH264High420, CodecTier::kHevc420};
    EXPECT_TRUE(negotiateCodec(local, remote) == CodecTier::kHevc420);
}

TEST(codec_negotiation, both_full_ladder_yields_av1) {
    std::vector<CodecTier> full = {CodecTier::kH264High420, CodecTier::kHevc420,
                                   CodecTier::kHevc444, CodecTier::kAv1};
    EXPECT_TRUE(negotiateCodec(full, full) == CodecTier::kAv1);
}

TEST(codec_negotiation, floor_only_yields_floor) {
    // Two conformant-but-minimal peers fall back to the H.264 floor.
    EXPECT_TRUE(negotiateCodec(floorCapabilities(), floorCapabilities()) ==
                CodecTier::kH264High420);
}

TEST(codec_negotiation, order_independent) {
    // Negotiation must not depend on the order tiers are advertised in.
    std::vector<CodecTier> a = {CodecTier::kAv1, CodecTier::kH264High420};
    std::vector<CodecTier> b = {CodecTier::kHevc444, CodecTier::kH264High420,
                                CodecTier::kAv1};
    EXPECT_TRUE(negotiateCodec(a, b) == CodecTier::kAv1);
    EXPECT_TRUE(negotiateCodec(b, a) == CodecTier::kAv1);  // symmetric
}

TEST(codec_negotiation, no_common_tier_is_failure) {
    // A non-conformant peer advertising only AV1 vs only the floor -> no common.
    std::vector<CodecTier> only_av1 = {CodecTier::kAv1};
    std::vector<CodecTier> only_floor = {CodecTier::kH264High420};
    EXPECT_TRUE(negotiateCodec(only_av1, only_floor) == CodecTier::kNone);
}

TEST(codec_negotiation, hevc444_preferred_over_hevc420) {
    // 4:4:4 (crisp text) outranks 4:2:0 when both ends carry it (ADR §3.2).
    std::vector<CodecTier> local = {CodecTier::kHevc420, CodecTier::kHevc444};
    std::vector<CodecTier> remote = {CodecTier::kHevc420, CodecTier::kHevc444};
    EXPECT_TRUE(negotiateCodec(local, remote) == CodecTier::kHevc444);
}

REDESK_TEST_MAIN()
