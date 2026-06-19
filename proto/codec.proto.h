#pragma once

// REDesk wire protocol — codec tiers & negotiation.
//
// ADR-001 §3.2 (Encode / Decode).
//
// Codec selection is *bidirectionally negotiated* in the control-channel Hello
// exchange, so the tier enum and the negotiation rule are part of the wire
// contract and live here in proto/ (consumed by core/codec, service/, ui/).
//
// Tier ladder (ADR §3.2), strictly ordered worst -> best:
//   H.264 High 4:2:0  — the always-present hardware FLOOR. Never ship without it.
//   HEVC 4:2:0        — preferred upgrade (~25-40% bitrate win).
//   HEVC 4:4:4        — crisp-text tier; 4:4:4 is bound EXCLUSIVELY to HEVC
//                       (H.264 4:4:4 has zero HW decode on any vendor — dropped).
//   AV1               — opportunistic, HW-only, when both ends pass a probe.
//
// IMPORTANT (ADR §3.2): every HW tier above the floor must be confirmed by a
// real one-frame test-encode/test-decode at the target profile/chroma before it
// is advertised — NEVER by marketing tier names. The negotiation here operates
// on already-probed capability sets.

#include <cstdint>
#include <vector>

namespace redesk::proto {

// Ordered worst -> best so the highest common tier is simply the max of the
// intersection. Integer values are wire format — append only, never renumber.
enum class CodecTier : uint8_t {
    kNone        = 0,  // sentinel: no common codec (negotiation failure)
    kH264High420 = 1,  // hardware floor (always present on a conformant build)
    kHevc420     = 2,  // preferred upgrade
    kHevc444     = 3,  // crisp-text 4:4:4 tier (HEVC only)
    kAv1         = 4,  // opportunistic, HW-only
};

inline constexpr CodecTier kFloorTier = CodecTier::kH264High420;
inline constexpr CodecTier kBestTier  = CodecTier::kAv1;

constexpr const char* codecTierName(CodecTier t) {
    switch (t) {
        case CodecTier::kNone:        return "none";
        case CodecTier::kH264High420: return "h264-high-420";
        case CodecTier::kHevc420:     return "hevc-420";
        case CodecTier::kHevc444:     return "hevc-444";
        case CodecTier::kAv1:         return "av1";
    }
    return "unknown";
}

// Negotiate the codec to use given each side's *probed* supported tiers.
// Picks the highest tier supported by BOTH ends (ADR §3.2 tiered negotiation).
// Returns kNone if there is no common tier — callers MUST treat that as a hard
// failure (a conformant build always advertises the floor, so kNone implies a
// misconfigured/non-conformant peer).
//
// Pure, dependency-free, and total: this is the function exercised by
// tests/test_codec_negotiation.
inline CodecTier negotiateCodec(const std::vector<CodecTier>& local,
                                const std::vector<CodecTier>& remote) {
    CodecTier best = CodecTier::kNone;
    for (CodecTier l : local) {
        if (l == CodecTier::kNone) continue;
        for (CodecTier r : remote) {
            if (l == r && static_cast<uint8_t>(l) > static_cast<uint8_t>(best)) {
                best = l;
            }
        }
    }
    return best;
}

// Convenience: the conformant-floor capability set every endpoint must offer.
inline std::vector<CodecTier> floorCapabilities() {
    return {CodecTier::kH264High420};
}

} // namespace redesk::proto
