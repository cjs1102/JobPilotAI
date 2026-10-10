"""Single-source platform-name registry (ExecPlan 0015A decision 4).

One data file is read by **both** the C++ service
(``service/src/job/job_quality.cpp`` -> ``PlatformNameRegistry``) and this
Python package::

    source_adapters/jobpilot_sources/data/platform_names.yaml

This module mirrors the C++ normalisation **byte for byte** rather than
re-implementing something similar, because a curated platform match decides
whether a job is queued for review:

===============  ==========================================================
Trim             ASCII whitespace only (space, ``\\t``, ``\\n``, ``\\v``,
                 ``\\f``, ``\\r``) - exactly what ``std::isspace`` matches in
                 the C locale. Unicode whitespace is **not** trimmed.
Fold             ``A``-``Z`` -> ``a``-``z`` only (``std::tolower`` on one
                 byte). Unicode case folding is **not** applied.
Strip suffixes   ``招聘网``, ``人才网``, ``官网``, ``招聘``, ``网`` - tested in
                 that order and repeated until no suffix matches, never
                 reducing the name to an empty string (the C++ guard is
                 ``normalized.size() > suffix.size()``, which is equivalent to
                 "a non-empty prefix remains" in both languages).
Contains         normalise the argument first, then exact set membership, so a
                 value carrying a stripped suffix still matches its registered
                 entry.
Unknown names    fail open: never a match, never an error.
===============  ==========================================================

The same file also carries ``platform_hosts`` (one curated host-suffix list per
platform). The host layer mirrors ``NormalizeHost`` / ``HostOf`` /
``HostConfirmsPlatform``:

- a host is ASCII-trimmed and folded, then trailing dots and a leading ``www.``
  are removed, in that order;
- ``source_url_host`` extracts the authority of an absolute URL and strips
  userinfo and port exactly like the C++ ``HostOf`` (a URL without ``://`` has
  no host);
- a host confirms a platform when it equals one of that platform's suffixes or
  ends with ``.<suffix>`` (a real label boundary, never a bare string suffix);
- the mapping is keyed by **normalised platform name**, never by host, so two
  registry names (aliases) may share one host and each confirms it
  independently, with no order dependence.

Maintainer note: the C++ reader of this section is line based, **not a YAML
parser**. It reads plain ``key: a.com, b.com`` scalars and normalises each
comma-separated entry once. A YAML list is ignored by both languages (identical
behaviour), but a *quoted* value or an inline ``# comment`` is read differently
there: C++ keeps the quote/comment characters as part of the entry, which then
never matches a real host, while PyYAML strips them. `tests/test_registry.py`
guards the shipped file against those two divergent forms; supporting them would
need the C++ side to parse the file with yaml-cpp.

The path is resolved the same way on both sides: the environment variable
``JOB_QUALITY_PLATFORM_NAMES_FILE`` wins, otherwise the repo-relative default
``source_adapters/jobpilot_sources/data/platform_names.yaml`` is used (the
service is documented to run from the repository root). Python additionally
falls back to the packaged copy of the *same* file, so an installed wheel can
still resolve it without shipping a second list. A missing, unreadable or
invalid file yields an empty registry plus a warning - fail-open, like C++.
"""

from __future__ import annotations

import functools
import os
from dataclasses import dataclass, replace
from pathlib import Path
from typing import Any, Mapping, Sequence

import yaml

#: Same name as ``kPlatformNamesFileEnv`` in job_quality.h.
PLATFORM_NAMES_FILE_ENV = "JOB_QUALITY_PLATFORM_NAMES_FILE"

#: Same value as ``kDefaultPlatformNamesFile`` in job_quality.h.
DEFAULT_PLATFORM_NAMES_FILE = (
    "source_adapters/jobpilot_sources/data/platform_names.yaml"
)

#: The packaged copy of the single file (identical content, no second list).
PACKAGE_DATA_FILE = (
    Path(__file__).resolve().parent / "data" / "platform_names.yaml"
)

#: ``std::isspace`` in the C locale: space, tab, newline, vertical tab, form
#: feed, carriage return. Deliberately not ``str.strip()``.
ASCII_WHITESPACE = " \t\n\r\v\f"

#: Order and set exactly mirror the C++ suffix table.
SUFFIXES = ("招聘网", "人才网", "官网", "招聘", "网")

_ASCII_LOWER = {ord(character): ord(character) + 32 for character in
                "ABCDEFGHIJKLMNOPQRSTUVWXYZ"}


def ascii_trim(value: str) -> str:
    """Trim the six ASCII whitespace characters only (C-locale ``isspace``)."""

    return value.strip(ASCII_WHITESPACE)


def ascii_lower(value: str) -> str:
    """Lower-case ``A``-``Z`` only; non-ASCII characters are untouched."""

    return value.translate(_ASCII_LOWER)


def normalize_platform_name(value: Any) -> str:
    """Normalise a company name exactly like ``PlatformNameRegistry::Normalize``."""

    if not isinstance(value, str):
        return ""

    text = ascii_lower(ascii_trim(value))

    stripped = True

    while stripped:
        stripped = False

        for suffix in SUFFIXES:
            # The C++ guard compares byte sizes; because a match means the
            # value ends with this exact suffix, `size() > suffix.size()` is
            # equivalent to "a non-empty prefix remains" in characters too.
            if text.endswith(suffix) and len(text) > len(suffix):
                text = text[: len(text) - len(suffix)]
                stripped = True
                break

    return text


def normalize_host(value: Any) -> str:
    """Normalise a host exactly like the C++ ``NormalizeHost``.

    Trim (ASCII) -> ASCII fold -> strip trailing dots -> strip one leading
    ``www.``. Ports and userinfo are **not** handled here: the C++ engine
    removes those in ``HostOf`` before calling this (see
    :func:`source_url_host`).
    """

    if not isinstance(value, str):
        return ""

    text = ascii_lower(ascii_trim(value))

    while text.endswith("."):
        text = text[:-1]

    if text.startswith("www."):
        text = text[4:]

    return text


def source_url_host(url: Any) -> str:
    """Host of an absolute URL, mirroring the C++ ``HostOf``.

    Requires ``://``; takes the authority up to the first ``/``, ``?`` or
    ``#``; strips userinfo up to the first ``@`` and the port from the first
    ``:``; then applies :func:`normalize_host`.
    """

    if not isinstance(url, str):
        return ""

    scheme = url.find("://")

    if scheme < 0:
        return ""

    start = scheme + 3
    end = len(url)

    for index in range(start, len(url)):
        if url[index] in "/?#":
            end = index
            break

    authority = url[start:end]
    at = authority.find("@")

    if at >= 0:
        authority = authority[at + 1 :]

    colon = authority.find(":")

    if colon >= 0:
        authority = authority[:colon]

    return normalize_host(authority)


def strip_quotes(value: str) -> str:
    """Strip one matching pair of surrounding quotes (C++ reader parity)."""

    if (
        len(value) >= 2
        and value[0] in "\"'"
        and value[-1] == value[0]
    ):
        return value[1:-1]

    return value


def _host_entries(value: Any) -> tuple[str, ...]:
    """Parse one ``platform_hosts`` value: comma-separated host suffixes.

    Mirrors the C++ line reader exactly: only a comma-separated string carries
    entries. A YAML list is ignored (the C++ reader ends the section on a
    ``- item`` line without a colon), so neither language invents a mapping the
    other does not have.
    """

    if not isinstance(value, str):
        return ()

    entries: list[str] = []

    for raw in value.split(","):
        entry = normalize_host(strip_quotes(ascii_trim(raw)))

        if entry:
            entries.append(entry)

    return tuple(entries)


@dataclass(frozen=True)
class PlatformNameRegistry:
    """A normalised, curated platform-name and host-suffix registry."""

    names: frozenset[str] = frozenset()
    #: ``((normalised platform, (normalised host suffix, ...)), ...)`` in file
    #: order; a later duplicate key wins, like ``hosts_[key] = entries`` in C++.
    hosts: tuple[tuple[str, tuple[str, ...]], ...] = ()
    source_path: str | None = None
    warning: str = ""

    @functools.cached_property
    def _host_index(self) -> Mapping[str, tuple[str, ...]]:
        return dict(self.hosts)

    @property
    def size(self) -> int:
        return len(self.names)

    @property
    def host_mapping_count(self) -> int:
        """Mirror of the C++ ``HostMappingCount`` (distinct platform keys)."""

        return len(self.hosts)

    def contains(self, company: Any) -> bool:
        """Normalise then exact-match; an unknown name never matches."""

        normalized = normalize_platform_name(company)

        return bool(normalized) and normalized in self.names

    def host_suffixes(self, normalized_platform: Any) -> tuple[str, ...]:
        """Registered host suffixes for a normalised platform name."""

        if not isinstance(normalized_platform, str) or not normalized_platform:
            return ()

        return self._host_index.get(normalized_platform, ())

    def host_confirms_platform(
        self,
        host: Any,
        normalized_platform: Any,
    ) -> bool:
        """Mirror of ``HostConfirmsPlatform``: exact or subdomain, never fuzzy.

        Lookup is keyed by the normalised platform name, so every alias sharing
        a host confirms independently (the C++ calls this alias-safe).
        """

        normalized = normalize_host(host)

        if not normalized:
            return False

        if not isinstance(normalized_platform, str) or not normalized_platform:
            return False

        for candidate in self.host_suffixes(normalized_platform):
            if not candidate:
                continue

            if normalized == candidate:
                return True

            if (
                len(normalized) > len(candidate)
                and normalized.endswith(candidate)
                and normalized[len(normalized) - len(candidate) - 1] == "."
            ):
                return True

        return False

    @classmethod
    def from_names(
        cls,
        names: Sequence[Any],
        *,
        hosts: Mapping[Any, Any] | Sequence[tuple[Any, Any]] | None = None,
        source_path: str | None = None,
    ) -> "PlatformNameRegistry":
        normalized = {
            normalize_platform_name(name)
            for name in names
        }
        normalized.discard("")

        pairs: list[tuple[str, tuple[str, ...]]] = []

        if hosts:
            for key, value in dict(hosts).items():
                name = normalize_platform_name(strip_quotes(str(key)))
                entries = (
                    _host_entries(value)
                    if isinstance(value, str)
                    else tuple(
                        normalize_host(strip_quotes(str(item)))
                        for item in value
                    )
                )
                entries = tuple(entry for entry in entries if entry)

                if name and entries:
                    pairs.append((name, entries))

        return cls(
            names=frozenset(normalized),
            hosts=tuple(pairs),
            source_path=source_path,
        )

    @classmethod
    def load(cls, path: str | os.PathLike[str]) -> "PlatformNameRegistry":
        """Load the registry; never raises (fail-open with a warning)."""

        text_path = str(path)

        try:
            with open(path, "r", encoding="utf-8") as handle:
                document = yaml.safe_load(handle)
        except OSError:
            return cls(
                source_path=text_path,
                warning=f"platform-name registry unavailable: {text_path}",
            )
        except yaml.YAMLError:
            return cls(
                source_path=text_path,
                warning=(
                    "platform-name registry is not valid YAML: "
                    f"{text_path}"
                ),
            )

        raw: Any = None
        raw_hosts: Any = None

        if isinstance(document, Mapping):
            raw = document.get("platform_names")
            raw_hosts = document.get("platform_hosts")

        names = raw if isinstance(raw, list) else []
        registry = cls.from_names(names, source_path=text_path)

        if isinstance(raw_hosts, Mapping):
            host_pairs: list[tuple[str, tuple[str, ...]]] = []

            for key, value in raw_hosts.items():
                # Exactly one NormalizeHost per entry, like the C++ reader: the
                # parsing lives here so a value is never normalised twice.
                entries = _host_entries(value)
                name = normalize_platform_name(strip_quotes(str(key)))

                if name and entries:
                    host_pairs.append((name, entries))

            if host_pairs:
                registry = replace(registry, hosts=tuple(host_pairs))

        if not names:
            return replace(
                registry,
                warning=(
                    "platform-name registry has no platform_names entries: "
                    f"{text_path}"
                ),
            )

        return registry

    @classmethod
    def load_from_environment(
        cls,
        environ: Mapping[str, str] | None = None,
    ) -> "PlatformNameRegistry":
        """Resolve the configured path, then load it (C++ parity)."""

        return cls.load(resolve_platform_names_path(environ))


def resolve_platform_names_path(
    environ: Mapping[str, str] | None = None,
) -> str:
    """Resolve the registry path exactly like ``LoadFromEnvironment``.

    1. ``JOB_QUALITY_PLATFORM_NAMES_FILE`` when set and non-empty;
    2. the repo-relative default, when it exists relative to the current
       working directory;
    3. the packaged copy of the same file (installed-wheel fallback);
    4. the repo-relative default string (``load`` then warns and fails open).
    """

    environment = os.environ if environ is None else environ
    configured = environment.get(PLATFORM_NAMES_FILE_ENV)

    if isinstance(configured, str) and configured.strip():
        return configured.strip()

    if Path(DEFAULT_PLATFORM_NAMES_FILE).is_file():
        return DEFAULT_PLATFORM_NAMES_FILE

    if PACKAGE_DATA_FILE.is_file():
        return str(PACKAGE_DATA_FILE)

    return DEFAULT_PLATFORM_NAMES_FILE


def load_platform_registry(
    environ: Mapping[str, str] | None = None,
) -> PlatformNameRegistry:
    """Convenience wrapper around :meth:`PlatformNameRegistry.load_from_environment`."""

    return PlatformNameRegistry.load_from_environment(environ)


__all__ = [
    "ASCII_WHITESPACE",
    "DEFAULT_PLATFORM_NAMES_FILE",
    "PACKAGE_DATA_FILE",
    "PLATFORM_NAMES_FILE_ENV",
    "SUFFIXES",
    "PlatformNameRegistry",
    "ascii_lower",
    "ascii_trim",
    "load_platform_registry",
    "normalize_host",
    "normalize_platform_name",
    "resolve_platform_names_path",
    "source_url_host",
    "strip_quotes",
]
