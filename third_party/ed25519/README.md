# ed25519 (orlp)

Compact, portable Ed25519 implementation used by the standalone runner package
signature.

- Upstream: https://github.com/orlp/ed25519
- Vendored from commit `master` (2026-11-17).
- License: zlib-style permissive (`license.txt`), compatible with Arestrix's MIT license.
- Files: the upstream `src/` implementation, excluding `seed.c` (Arestrix supplies
  the private seed from an external secret and does not call `ed25519_create_seed`).
- API used: `ed25519_create_keypair`, `ed25519_sign`, `ed25519_verify`.
