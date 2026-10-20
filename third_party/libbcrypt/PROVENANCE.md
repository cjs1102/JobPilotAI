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

## Revision record (2026-10-20)

An upstream revision could **not** be resolved from this development host:
the GitHub API and `git ls-remote` both failed at the TLS/network layer
(`SSL_ERROR_SYSCALL`; `gnutls_handshake() failed: The TLS connection was
non-properly terminated`). No commit/tag is invented here. The check should be
retried from a host with GitHub access; if a revision is confirmed, replace this
section with the commit/tag and retrieval date.

For reproducibility, the vendored source content at this checkout hashes as:

```text
dddc287acb7fcbf175b5261d9b4b5942b6ee0d0aa95a5df53a280868ed4082d7  bcrypt.c
8db538bdb3cea7759752f78c080bdbe47972e627ccad41d33e8dd280a32b8110  bcrypt.h
c980cb48803d574b3ac14f5597feb57a0c39d57a25337a6a43379744b5e95411  crypt_blowfish.c
1307f3579b0e884c57aa7098be2a49304154db4b63e0a758adb1c3f3e38b600f  crypt_blowfish.h
3359cac7170f51a76bac21f429b9799f942c4b187a27021310dd3b4c5e45559e  crypt_gensalt.c
a147a4128035407f73a483c6afd955334252c7da3d88bee532a47f71aff4a2cc  crypt_gensalt.h
77021ff67fbb4fbec70c77c2853f84981982f377de16702926ebd0e7e67912b9  ow-crypt.h
36565e0b2259c7434cf8df450a65f6986ba705dcb7bd9426a509e26dc2170c85  winbcrypt.h
68714cca5c4c5c41ec0363afde7ef0a4831726bf8f313ab586f43d45e74e83df  wrapper.c
51344234befd4360389af418964b2d23375c816120ecadac47c1b80ea73e8506  CMakeLists.txt
```

The hashes identify the vendored content; they are not an upstream version
statement. `PROVENANCE.md` intentionally remains honest about the unresolved
revision rather than claiming `master` or a guessed commit.
