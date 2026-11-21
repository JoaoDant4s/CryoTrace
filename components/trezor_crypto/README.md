# trezor_crypto (vendored subset)

Minimal subset of [trezor-crypto](https://github.com/trezor/trezor-firmware/tree/main/crypto)
(MIT, see `LICENSE`) used for secp256k1 signing and Keccak-256.

- Upstream commit: `98deb5b04179079d8f2a6655410a124e5e95fbda` (2026-11-20)
- Files in `crypto/` are copied unchanged.
- Only the signing and hashing code is compiled. `address.h`, `base58.h`,
  `bip32.h`, `hasher.h`, `script.h` and the other cryptocurrency headers are
  here only because `ecdsa.h` / `bignum.c` include them; their implementations
  are not part of the build, and the unused functions that reference them are
  dropped at link time.
- `port/random_esp.c` provides the platform `random_buffer()` from the ESP32
  hardware RNG.

To update: copy the same file list from a newer upstream commit and rerun
`make -C test/host` and `idf.py build`.
