#pragma once

// REDesk rendezvous control protocol (ADR-001 §2, §3.3, §3.6).
//
// PROTOCOL OVERVIEW
// -----------------
// The rendezvous server is pure signaling. It maps a REDesk-ID (a routing
// handle only — the *identity* is the X25519 static-key fingerprint, §3.6.1)
// to a reachable endpoint, brokers ICE candidates / SDP between two peers, and
// Ed25519-signs each peer's static pubkey so the other side can detect MITM
// *before* the Noise handshake (the signature is a separate out-of-band layer,
// NOT part of Noise, which is DH-only — §3.6.3). The server has ZERO knowledge
// of media keys: it never sees ephemeral/static private keys, Noise session
// keys, or plaintext (§3.6.2).
//
// This mirrors RustDesk's hbbs role but is signed-introducer + zero-knowledge;
// it does NOT hold the static-static crypto_box keys RustDesk's design implies.
//
// Three message families travel on the control connection (UDP for keepalive /
// punch coordination, TCP for reliable register/lookup; §3.3 keeps a TCP/TLS-443
// path for UDP-blocked networks):
//
//   1. REGISTER   peer -> server
//        { redesk_id, static_pubkey(32B X25519), observed-endpoint(implicit
//          from packet source), capabilities }
//      The server records {endpoint, static_pubkey, last_seen}, replies with
//      REGISTER_OK carrying the server's Ed25519 signature over the peer's own
//      static_pubkey (so the peer can forward a server-attested copy) plus the
//      server's Ed25519 verify key (pinned out-of-band / shipped with client).
//
//   2. LOOKUP     controller -> server
//        { target_redesk_id }
//      Server replies LOOKUP_OK { endpoint, static_pubkey, server_signature }
//      or LOOKUP_FAIL { reason }. The controller verifies the signature with
//      the pinned server verify-key before trusting the pubkey, then pins it
//      (TOFU + key-change alerts, §3.6.3).
//
//   3. BROKER_CANDIDATES   bidirectional, server-relayed during ICE setup
//        Opaque ICE-candidate / SDP blobs are relayed between the two peers
//        keyed by a short-lived session token. The server treats the blobs as
//        opaque and only routes them; the actual hole-punch + Noise handshake
//        happen peer-to-peer (§3.3 ICE tiers host -> srflx -> relayed). On punch
//        failure the server hands back the lowest-RTT relay (see redesk-relay).
//
// Wire framing in the real build is the shared proto/ definition; here we keep
// a minimal length-prefixed tag enum so the listener skeleton is concrete and
// testable without the full codec. TODO(ADR §3.3/§5): replace with the
// proto/-generated framing (one source of truth for wire + IPC).

#include <cstdint>
#include <string>
#include <vector>

#include "core/common/types.h"

namespace redesk::server::rendezvous {

// A REDesk-ID is a routing handle (e.g. a 9-digit code or short string). It is
// NOT the cryptographic identity — that is the BLAKE2b fingerprint of the
// static pubkey (§3.6.1). Kept as a string so format can evolve.
using RedeskId = std::string;

// X25519 static public key, 32 bytes (§3.6.1). Stored/forwarded opaque.
inline constexpr std::size_t kStaticPubKeyLen = 32;
using StaticPubKey = std::vector<std::uint8_t>;

// Control message tags on the rendezvous connection.
enum class MessageType : std::uint8_t {
    Unknown = 0,
    Register = 1,        // peer -> server
    RegisterOk = 2,      // server -> peer (carries server signature + verify key)
    Lookup = 3,          // controller -> server
    LookupOk = 4,        // server -> controller
    LookupFail = 5,      // server -> controller
    BrokerCandidates = 6,// peer <-> peer (server-relayed, opaque blob)
    Keepalive = 7,       // peer -> server (refreshes last_seen / NAT binding)
};

// Parsed REGISTER payload (endpoint comes from the packet source, not the body,
// so a peer can't spoof another peer's reachable address).
struct RegisterRequest {
    RedeskId id;
    StaticPubKey static_pubkey; // 32B X25519
    std::uint32_t capabilities = 0;
};

// Parsed LOOKUP payload.
struct LookupRequest {
    RedeskId target;
};

// Minimal length-prefixed framing helpers for the skeleton control listener.
// Layout: [u8 type][u8 id_len][id bytes][u16 body_len][body bytes]. Real build
// uses proto/. Returns Unknown/empty on malformed input (caller drops).
struct Frame {
    MessageType type = MessageType::Unknown;
    std::string id;                 // REDesk-ID (may be empty for some types)
    std::vector<std::uint8_t> body; // type-specific payload (opaque blobs, keys)
};

// Encode/decode are intentionally tolerant: malformed frames decode to
// type==Unknown so the listener can drop without crashing on hostile input.
Result<Frame> decode_frame(const std::uint8_t* data, std::size_t len);
std::vector<std::uint8_t> encode_frame(const Frame& f);

} // namespace redesk::server::rendezvous
