# REDesk servers (ADR-001 §2, §3.3, §3.6).
#
# Two horizontally-scalable, multi-region server roles that map 1:1 onto
# RustDesk's hbbs/hbbr split (see server/README.md):
#
#   rendezvous/  -> redesk-rendezvous : signaling. ID->endpoint registry, ICE
#                   candidate / SDP broker, Ed25519-signs each peer's static
#                   pubkey. ZERO knowledge of media keys. (== RustDesk hbbs)
#
#   relay/       -> redesk-relay      : thin blind-forward relay. Forwards
#                   opaque Noise ciphertext between two endpoints by session
#                   token; never decrypts. Punch-fail fallback for ~15-30%+ of
#                   sessions. (== RustDesk hbbr)
#
# Both link REDesk::core for shared value types only. Both compile in the
# default stub build with NO external dependencies (std + a portable BSD-socket
# wrapper). Real crypto / production hardening is gated behind
# REDESK_USE_REAL_BACKENDS (see rendezvous/signer.* for the libsodium path).

# Shared, dependency-free BSD-socket wrapper used by both servers.
add_subdirectory(common)

add_subdirectory(rendezvous)
add_subdirectory(relay)
