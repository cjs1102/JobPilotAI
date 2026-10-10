# Third-party notices

JobPilotAI itself is MIT licensed — see [`LICENSE`](LICENSE). This file lists the
third-party components the project ships or depends on, and where their terms
live. It does not restate licence texts that the packages themselves carry.

## Vendored source

### `third_party/libbcrypt`

- Wrapper library: [`rg3/libbcrypt`](https://github.com/rg3/libbcrypt) — released
  to the public domain under CC0.
- Password hashing implementation: Openwall
  [`crypt_blowfish`](https://www.openwall.com/crypt/) by Solar Designer — public
  domain dedication with a permissive fallback licence.

Both identifications, and the note that the full licence text stays embedded in
the source headers, come from
[`third_party/libbcrypt/PROVENANCE.md`](third_party/libbcrypt/PROVENANCE.md).

## C++ dependencies (Conan)

Declared in [`service/conanfile.txt`](service/conanfile.txt) and pinned by
[`service/conan.lock`](service/conan.lock); fetched from ConanCenter when the
build bootstraps, so none of them is vendored into this repository.

| Component | Upstream | Licence |
|---|---|---|
| spdlog | <https://github.com/gabime/spdlog> | MIT (verified in the pinned package's `licenses/LICENSE`) |
| fmt | <https://github.com/fmtlib/fmt> | MIT (verified in the pinned package's `licenses/LICENSE`) |
| yaml-cpp | <https://github.com/jbeder/yaml-cpp> | MIT (verified in the pinned package's `licenses/LICENSE`) |
| nlohmann/json | <https://github.com/nlohmann/json> | MIT (verified in the pinned package's `licenses/LICENSE.MIT`) |
| GoogleTest | <https://github.com/google/googletest> | BSD-3-Clause (verified in the pinned package's `licenses/`) |
| hiredis | <https://github.com/redis/hiredis> | BSD-3-Clause (verified in the pinned package's `licenses/COPYING`) |

One dependency is not from Conan: the MySQL client library, provided by the
distribution (`libmysqlclient-dev` to build, `libmysqlclient21` at run time).
**Its licence is GPLv2 with Oracle's "FOSS License Exception"** — the Debian
package ships that text in `/usr/share/doc/libmysqlclient21/copyright`. That is
the one term in this file of a different kind, so it is named explicitly here:
the `service/` binary and any image built from `Dockerfile` link that library and
are therefore a combined work with it, which is a distribution decision the
maintainer has to make (the release checklist's publish-safety review lists the
options). The repository's own source stays MIT.

## Python dependencies

- [`source_adapters/pyproject.toml`](source_adapters/pyproject.toml) and
  [`ai_worker/pyproject.toml`](ai_worker/pyproject.toml) depend on PyYAML (MIT —
  see the upstream `LICENSE`).
- Everything under [`tools/`](tools/) uses the Python standard library only.

## Container images

[`docker-compose.yml`](docker-compose.yml) references the official `mysql:8.0`
and `redis:7` images. They run as separate services and are not linked into this
codebase; each carries its own licence, and both projects have changed their
terms across versions, so read the licence that ships with the image tag you
pin.
