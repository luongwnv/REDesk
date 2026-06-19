// test_transport_loopback — in-process loopback transport delivers on the right
// channel with correct per-channel reliability semantics (ADR-001 §3.3).
//
// Asserts:
//   * a frame sent on a channel is delivered to the peer's handler for THAT
//     channel (multiplexing over one 5-tuple), with header fields intact;
//   * reliable-ordered channels (input/control/file) preserve FIFO order and
//     never drop — input is never head-of-line-blocked behind other channels;
//   * unreliable channels (audio) honor drop-late (a dropped frame is gone, no
//     retransmit), matching the video/audio "drop stale" policy.

#include <vector>

#include "proto/transport.proto.h"
#include "tests/redesk_test.h"
#include "tests/stub_backends.h"

using redesk::proto::Channel;
using redesk::proto::MessageType;
using redesk::proto::Reliability;
using redesk::proto::reliabilityFor;
using redesk::test::stub::DeliveredFrame;
using redesk::test::stub::LoopbackTransport;

namespace {

std::vector<uint8_t> bytes(std::initializer_list<uint8_t> b) { return {b}; }

} // namespace

TEST(transport, delivers_on_correct_channel) {
    LoopbackTransport viewer, host;
    LoopbackTransport::link(viewer, host);

    std::vector<DeliveredFrame> input_rx;
    std::vector<DeliveredFrame> video_rx;
    host.onChannel(Channel::kInput,
                   [&](const DeliveredFrame& f) { input_rx.push_back(f); });
    host.onChannel(Channel::kVideo,
                   [&](const DeliveredFrame& f) { video_rx.push_back(f); });

    // Viewer sends an input event; it must land ONLY on the host's input channel.
    auto st = viewer.send(Channel::kInput, MessageType::kInputEvent,
                          bytes({0xAA, 0xBB}));
    EXPECT_TRUE(st.ok());

    ASSERT_EQ(input_rx.size(), size_t{1});
    EXPECT_EQ(video_rx.size(), size_t{0});  // no cross-channel leakage
    EXPECT_TRUE(input_rx[0].header.channel == Channel::kInput);
    EXPECT_TRUE(input_rx[0].header.type == MessageType::kInputEvent);
    ASSERT_EQ(input_rx[0].payload.size(), size_t{2});
    EXPECT_EQ(input_rx[0].payload[0], uint8_t{0xAA});
    EXPECT_EQ(input_rx[0].payload[1], uint8_t{0xBB});
}

TEST(transport, reliable_channel_preserves_order) {
    LoopbackTransport a, b;
    LoopbackTransport::link(a, b);

    std::vector<uint32_t> seqs;
    b.onChannel(Channel::kInput,
                [&](const DeliveredFrame& f) { seqs.push_back(f.header.seq); });

    // Input is reliable-ordered (ADR §3.3): every frame arrives, in order.
    EXPECT_TRUE(reliabilityFor(Channel::kInput) == Reliability::kReliableOrdered);
    for (int i = 0; i < 5; ++i) {
        a.send(Channel::kInput, MessageType::kInputEvent,
               bytes({static_cast<uint8_t>(i)}));
    }
    ASSERT_EQ(seqs.size(), size_t{5});
    for (uint32_t i = 0; i < 5; ++i) EXPECT_EQ(seqs[i], i);
}

TEST(transport, input_not_blocked_by_file_transfer) {
    // Independent per-channel streams: a (large) file transfer in flight must
    // not delay input delivery (no head-of-line blocking — ADR §3.3).
    LoopbackTransport a, b;
    LoopbackTransport::link(a, b);

    int file_count = 0;
    bool input_arrived = false;
    b.onChannel(Channel::kFile, [&](const DeliveredFrame&) { ++file_count; });
    b.onChannel(Channel::kInput, [&](const DeliveredFrame&) { input_arrived = true; });

    a.send(Channel::kFile, MessageType::kFileChunk, std::vector<uint8_t>(4096, 0x5A));
    a.send(Channel::kInput, MessageType::kInputEvent, bytes({0x01}));
    a.send(Channel::kFile, MessageType::kFileChunk, std::vector<uint8_t>(4096, 0x5A));

    EXPECT_EQ(file_count, 2);
    EXPECT_TRUE(input_arrived);  // input delivered despite file chunks around it
}

TEST(transport, unreliable_channel_drops_late) {
    LoopbackTransport a, b;
    LoopbackTransport::link(a, b);

    int audio_count = 0;
    b.onChannel(Channel::kAudio, [&](const DeliveredFrame&) { ++audio_count; });

    // Audio is unreliable/drop-late (ADR §3.3): tell b to drop the next inbound
    // audio frame; it must NOT be retransmitted.
    EXPECT_TRUE(reliabilityFor(Channel::kAudio) == Reliability::kUnreliable);
    b.dropNextInbound(Channel::kAudio, 1);

    a.send(Channel::kAudio, MessageType::kAudioFrame, bytes({0x10}));  // dropped
    a.send(Channel::kAudio, MessageType::kAudioFrame, bytes({0x20}));  // delivered

    EXPECT_EQ(audio_count, 1);  // exactly one survived; the dropped one is gone
}

TEST(transport, send_without_link_fails) {
    LoopbackTransport solo;  // not linked to a peer
    auto st = solo.send(Channel::kControl, MessageType::kKeepalive, {});
    EXPECT_FALSE(st.ok());
    EXPECT_TRUE(st.code == redesk::ErrorCode::ConnectionLost);
}

REDESK_TEST_MAIN()
