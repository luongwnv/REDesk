// test_codec_roundtrip — stub encoder -> stub decoder returns an equivalent
// frame (ADR-001 §3.2). Proves the encode/decode seam is wired and lossless on
// the stub path (a real HW path round-trips lossily; the stub is identity so the
// pipeline can be verified bit-exact in the dep-free build).

#include "tests/redesk_test.h"
#include "tests/stub_backends.h"

using namespace redesk;
using redesk::test::stub::makeStubDecoder;
using redesk::test::stub::makeStubEncoder;

namespace {

VideoFrame makeFrame() {
    VideoFrame f;
    f.size = {1280, 720};
    f.format = PixelFormat::NV12;
    f.timestamp_us = 42'000;
    f.cpu_pixels.resize(64);
    for (size_t i = 0; i < f.cpu_pixels.size(); ++i) {
        f.cpu_pixels[i] = static_cast<uint8_t>((i * 7 + 3) & 0xff);
    }
    return f;
}

} // namespace

TEST(codec, roundtrip_preserves_frame) {
    auto enc = makeStubEncoder();
    auto dec = makeStubDecoder();
    ASSERT_TRUE(enc != nullptr);
    ASSERT_TRUE(dec != nullptr);

    const VideoFrame original = makeFrame();

    auto encoded = enc->encode(original);
    ASSERT_TRUE(encoded.ok());
    EXPECT_TRUE(encoded.value.keyframe);  // stub frames are independently decodable
    EXPECT_EQ(encoded.value.timestamp_us, original.timestamp_us);
    EXPECT_FALSE(encoded.value.data.empty());

    auto decoded = dec->decode(encoded.value);
    ASSERT_TRUE(decoded.ok());

    const VideoFrame& got = decoded.value;
    EXPECT_EQ(got.size.width, original.size.width);
    EXPECT_EQ(got.size.height, original.size.height);
    EXPECT_TRUE(got.format == original.format);
    EXPECT_EQ(got.timestamp_us, original.timestamp_us);
    ASSERT_EQ(got.cpu_pixels.size(), original.cpu_pixels.size());
    EXPECT_TRUE(got.cpu_pixels == original.cpu_pixels);
}

TEST(codec, decode_rejects_garbage) {
    auto dec = makeStubDecoder();
    EncodedPacket junk;
    junk.data = {0x00, 0x01, 0x02};  // too small / no valid container
    auto decoded = dec->decode(junk);
    EXPECT_FALSE(decoded.ok());
    EXPECT_TRUE(decoded.status.code == ErrorCode::InvalidArgument);
}

REDESK_TEST_MAIN()
