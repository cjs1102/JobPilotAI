#!/usr/bin/env bash
# Produces the Conan toolchain that both CMake entries expect:
#
#   service/build/conan_toolchain.cmake   (read by CMakePresets.json and by
#                                          service/CMakePresets.json)
#
# A fresh clone has no such file - the Conan output is generated and gitignored -
# so `cmake --preset <name>` cannot configure until this script has run once.
# Running it again only re-resolves the pinned dependency graph; it never edits
# tracked files.
#
# Usage:
#   tools/dev/bootstrap.sh
#
# Overrides:
#   CONAN=/path/to/conan   use a specific Conan 2 binary
#   CONAN_HOME=/path       use an isolated Conan home (a fresh cache)
#   CONAN_REMOTE_NAME=name / CONAN_REMOTE_URL=url   override the Conan registry
#                          (default conancenter -> https://center2.conan.io)
#   OUTPUT_FOLDER=path     write the generated toolchain somewhere else
#                          (default service/build, which the presets read; the
#                          override exists so a fresh-cache check can resolve the
#                          graph without disturbing a working build tree)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

OUTPUT_FOLDER="${OUTPUT_FOLDER:-service/build}"
CONANFILE="service/conanfile.txt"
LOCKFILE="service/conan.lock"
BUILD_TYPE="Release"
# The project compiles as C++17 (`CMAKE_CXX_STANDARD 17` in CMakeLists.txt).
# Conan builds the dependencies with gnu17, the setting the verified build graph
# was resolved with (service/conan.lock); pinning it here keeps a host profile
# from silently changing which dependency binaries the build links.
CPPSTD="gnu17"

fail()
{
    printf 'bootstrap: %s\n' "$1" >&2
    exit 1
}

# --- prerequisites -----------------------------------------------------------

command -v cmake >/dev/null 2>&1 ||
    fail "cmake is not on PATH (the presets need >= 3.22)"
command -v pkg-config >/dev/null 2>&1 ||
    fail "pkg-config is not on PATH"
pkg-config --exists mysqlclient ||
    fail "the MySQL client development files are missing (pkg-config module 'mysqlclient'); install libmysqlclient-dev (Debian/Ubuntu) or mysql-devel (RHEL/Fedora)"
command -v c++ >/dev/null 2>&1 || command -v g++ >/dev/null 2>&1 ||
    fail "no C++ compiler on PATH"

CONAN_BIN="${CONAN:-}"
if [ -z "$CONAN_BIN" ]; then
    if command -v conan >/dev/null 2>&1; then
        CONAN_BIN="$(command -v conan)"
    elif [ -x "$HOME/.local/bin/conan" ]; then
        # pipx and `pip install --user` both land here, and neither is on a
        # non-interactive PATH.
        CONAN_BIN="$HOME/.local/bin/conan"
    else
        fail "conan 2 is not on PATH and \$HOME/.local/bin/conan does not exist; install it (e.g. 'pipx install conan')"
    fi
fi

[ -f "$CONANFILE" ] || fail "$CONANFILE is missing; run this from a repository checkout"

CONAN_HOME_DIR="${CONAN_HOME:-$HOME/.conan2}"
if [ ! -f "$CONAN_HOME_DIR/profiles/default" ]; then
    printf 'bootstrap: no Conan profile in %s, detecting one\n' "$CONAN_HOME_DIR"
    "$CONAN_BIN" profile detect
fi

# A fresh Conan home has no remotes at all, so the pinned graph cannot be
# resolved until the registry is declared. The default is the official
# ConanCenter; a mirror can be substituted with these two variables (the lock
# pins recipe revisions, which a faithful mirror serves identically).
CONAN_REMOTE_NAME="${CONAN_REMOTE_NAME:-conancenter}"
CONAN_REMOTE_URL="${CONAN_REMOTE_URL:-https://center.conan.io}"
if ! "$CONAN_BIN" remote list | grep -q "^${CONAN_REMOTE_NAME}:"; then
    printf 'bootstrap: adding Conan remote %s -> %s\n' "$CONAN_REMOTE_NAME" "$CONAN_REMOTE_URL"
    "$CONAN_BIN" remote add "$CONAN_REMOTE_NAME" "$CONAN_REMOTE_URL"
fi

# --- dependency graph --------------------------------------------------------

# The lockfile pins the recipe revisions of the direct and transitive
# requirements; --lockfile-partial lets a host whose compiler differs from the
# one that generated the lock complete the graph instead of failing, and
# --build=missing builds anything the cache does not already hold.
echo "bootstrap: $("$CONAN_BIN" --version) install ($BUILD_TYPE, cppstd=$CPPSTD) -> $OUTPUT_FOLDER"
extra_args=()
if [ "$OUTPUT_FOLDER" != "service/build" ]; then
    # A scratch output folder must not append its generated presets to the
    # source-tree CMakeUserPresets.json: two includes both define `conan-release`,
    # and CMake then refuses to read the presets at all ("Duplicate preset"),
    # which breaks `cmake --preset <name>` from service/.
    extra_args+=(-c "tools.cmake.cmaketoolchain:user_presets=$OUTPUT_FOLDER/CMakeUserPresets.json")
fi
"$CONAN_BIN" install "$CONANFILE" \
    --output-folder="$OUTPUT_FOLDER" \
    --build=missing \
    -s build_type="$BUILD_TYPE" \
    -s compiler.cppstd="$CPPSTD" \
    --lockfile="$LOCKFILE" \
    --lockfile-partial \
    ${extra_args[@]+"${extra_args[@]}"}

[ -f "$OUTPUT_FOLDER/conan_toolchain.cmake" ] ||
    fail "conan did not generate $OUTPUT_FOLDER/conan_toolchain.cmake"

cat <<'NEXT'

bootstrap: done. Next commands, from the repository root:

    cmake --preset release
    cmake --build --preset release -j"$(nproc)"

Both entries' presets (root and service/) read that one generated toolchain, so
the sanitizer presets reuse it as-is: substitute `asan`, `ubsan` or `tsan` for
`release`. The binary lands at <build-dir>/service/jobpilot_service.

NEXT
