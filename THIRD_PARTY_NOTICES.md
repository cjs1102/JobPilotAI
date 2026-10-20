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
That file also records the 2026-10-20 upstream-resolution attempt (GitHub/TLS
unavailable from the development host), the explicit “upstream revision
unresolved” status, and the SHA-256 manifest of the vendored sources.

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

One dependency is not from Conan: the MySQL **client** library, provided by the
distribution. The committed `Dockerfile` builds on `ubuntu:24.04` with
`libmariadb-dev` + `libmariadb-dev-compat` and installs `libmariadb3` at run
time (MariaDB Connector/C). The project needs no source change: CMake resolves
the same `mysqlclient` pkg-config name through the compat package, and the
binary carries `NEEDED libmariadb.so.3` (dynamic linking).

The MariaDB Connector/C *client library* is **LGPL-2.1-or-later**: the package's
own copyright file assigns `Files: libmariadb/*` to `License: LGPL-2+`, while
the GPL-2 stanzas in that same file cover the MariaDB *server* files. The image
workflow asserts the `libmariadb.so.3` linkage and the absence of
`libmysqlclient`, then runs the demo gate against MySQL 8.0. The previous Oracle
`libmysqlclient21` evidence remains in the release history; it is no longer the
shipped image. This is a factual provenance/linkage note, **not a legal
conclusion**; the maintainer decides distribution compliance.

## Python dependencies

- [`source_adapters/pyproject.toml`](source_adapters/pyproject.toml) and
  [`ai_worker/pyproject.toml`](ai_worker/pyproject.toml) depend on PyYAML (MIT —
  see the upstream `LICENSE`). The two per-package `requirements.lock` files pin
  the exact version and hashes; CI installs them with `--require-hashes`.
- Everything under [`tools/`](tools/) uses the Python standard library only.

## Container images

[`docker-compose.yml`](docker-compose.yml) references the official `mysql:8.0`,
`redis:7` and (opt-in `web` profile) `nginx:1.27-alpine` images. They run as
separate services and are not linked into this codebase; each carries its own
licence, and these projects have changed their terms across versions, so read
the licence that ships with the image tag you pin.
