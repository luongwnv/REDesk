// REDesk rendezvous control framing (ADR-001 §3.3).
//
// Minimal, hostile-input-tolerant length-prefixed codec for the skeleton
// listener. Replaced by the proto/-generated wire format in the real build.

#include "server/rendezvous/protocol.h"

namespace redesk::server::rendezvous {

namespace {
constexpr std::uint8_t kMaxType = static_cast<std::uint8_t>(MessageType::Keepalive);
}

Result<Frame> decode_frame(const std::uint8_t* data, std::size_t len) {
    Frame f;
    // [u8 type][u8 id_len][id][u16 body_len][body]
    std::size_t off = 0;
    auto need = [&](std::size_t n) { return off + n <= len; };

    if (!need(1)) return Result<Frame>::fail(ErrorCode::InvalidArgument, "short frame: type");
    std::uint8_t t = data[off++];
    if (t == 0 || t > kMaxType) {
        // Unknown/out-of-range type: decode as Unknown so caller drops cleanly.
        return Result<Frame>::good(Frame{});
    }
    f.type = static_cast<MessageType>(t);

    if (!need(1)) return Result<Frame>::fail(ErrorCode::InvalidArgument, "short frame: id_len");
    std::uint8_t id_len = data[off++];
    if (!need(id_len)) return Result<Frame>::fail(ErrorCode::InvalidArgument, "short frame: id");
    f.id.assign(reinterpret_cast<const char*>(data + off), id_len);
    off += id_len;

    if (!need(2)) return Result<Frame>::fail(ErrorCode::InvalidArgument, "short frame: body_len");
    std::uint16_t body_len = static_cast<std::uint16_t>((data[off] << 8) | data[off + 1]);
    off += 2;
    if (!need(body_len)) return Result<Frame>::fail(ErrorCode::InvalidArgument, "short frame: body");
    f.body.assign(data + off, data + off + body_len);

    return Result<Frame>::good(std::move(f));
}

std::vector<std::uint8_t> encode_frame(const Frame& f) {
    std::vector<std::uint8_t> out;
    out.reserve(4 + f.id.size() + f.body.size());
    out.push_back(static_cast<std::uint8_t>(f.type));

    const std::uint8_t id_len = static_cast<std::uint8_t>(
        f.id.size() > 255 ? 255 : f.id.size());
    out.push_back(id_len);
    out.insert(out.end(), f.id.begin(), f.id.begin() + id_len);

    const std::uint16_t body_len = static_cast<std::uint16_t>(
        f.body.size() > 0xFFFF ? 0xFFFF : f.body.size());
    out.push_back(static_cast<std::uint8_t>(body_len >> 8));
    out.push_back(static_cast<std::uint8_t>(body_len & 0xFF));
    out.insert(out.end(), f.body.begin(), f.body.begin() + body_len);

    return out;
}

} // namespace redesk::server::rendezvous
