# libbcrypt Provenance

This directory contains the bcrypt implementation used by `jobpilot_bcrypt`.

## Upstream Sources

- Wrapper library: `rg3/libbcrypt`
  - https://github.com/rg3/libbcrypt
- Password hashing implementation: Openwall `crypt_blowfish`
  - https://www.openwall.com/crypt/

The wrapper README describes the library as a reentrant bcrypt wrapper around
Solar Designer's implementation and states that the code is released to the
public domain under CC0.

## License Notices

- `bcrypt.c`, `bcrypt.h`, and related wrapper files identify Ricardo Garcia
  and the CC0 public-domain dedication.
- `crypt_blowfish.c`, `crypt_gensalt.c`, `crypt_blowfish.h`,
  `crypt_gensalt.h`, `ow-crypt.h`, and `wrapper.c` identify Solar Designer and
  Openwall, with public-domain dedication and a permissive fallback license.

The full license text remains embedded in the source headers.

## Build Use

`backend/CMakeLists.txt` adds this directory and links `jobpilot_bcrypt` to the
bcrypt test. The implementation is therefore a required vendored dependency,
not generated output.

## Local Changes

No separate local-modification record was present when this provenance file was
added. Before upgrading or replacing these files, compare them with the current
upstream sources and preserve their license headers.
