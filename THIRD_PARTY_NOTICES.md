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

One dependency is not from Conan: the MySQL **client** library, provided by the
distribution. The committed `Dockerfile` builds on `ubuntu:24.04` with
`libmysqlclient-dev` and installs `libmysqlclient21` at run time, i.e. Oracle's
client (MySQL 8.0.46 where this was checked). **Its licence is GPLv2 with Oracle's
"FOSS License Exception"** — the distribution package ships that text in
`/usr/share/doc/libmysqlclient21/copyright` (`License: GPL-2+` for the library
files; the exception is quoted in its first 25 lines). That is the one term in
this file of a different kind, so it is named explicitly: the `service/` binary
and any image built from `Dockerfile` link it and are a combined work with it,
which is a distribution decision the maintainer has to make.

**The evaluated alternative is MariaDB Connector/C** (`libmariadb-dev` +
`libmariadb-dev-compat` to build, `libmariadb3` at run time), whose *client
library* is **LGPL-2.1-or-later**: the package's own copyright file assigns
`Files: libmariadb/*` to `License: LGPL-2+`, while the GPL-2 stanzas in that same
file cover the MariaDB *server* files. The project needs no source change for it —
CMake resolves the same `mysqlclient` pkg-config name to MariaDB Connector/C
3.3.19 through the compat package, the binary then carries
`NEEDED libmariadb.so.3` (dynamic linking, which is the relink condition LGPL-2.1
asks for), and the Debian-based verification images that contain only
`libmariadb3` have already passed the real-capture acceptance and the demo gate
against MySQL 8.0. Switching the committed `Dockerfile` and the CI package lines
is prepared and recorded in `docs/RELEASE_CHECKLIST.md` §2; a dependency change is
applied only together with the compatibility evidence recorded there, and none of
this is a legal conclusion — the maintainer decides.

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
