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

The path is resolved the same way on both sides: the environment variable
``JOB_QUALITY_PLATFORM_NAMES_FILE`` wins, otherwise the repo-relative default
``source_adapters/jobpilot_sources/data/platform_names.yaml`` is used (the
service is documented to run from the repository root). Python additionally
falls back to the packaged copy of the *same* file, so an installed wheel can
still resolve it without shipping a second list. A missing, unreadable or
invalid file yields an empty registry plus a warning - fail-open, like C++.
"""

from __future__ import annotations

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


@dataclass(frozen=True)
class PlatformNameRegistry:
    """A normalised, curated set of platform names (fail-open on unknown)."""

    names: frozenset[str] = frozenset()
    source_path: str | None = None
    warning: str = ""

    @property
    def size(self) -> int:
        return len(self.names)

    def contains(self, company: Any) -> bool:
        """Normalise then exact-match; an unknown name never matches."""

        normalized = normalize_platform_name(company)

        return bool(normalized) and normalized in self.names

    @classmethod
    def from_names(
        cls,
        names: Sequence[Any],
        *,
        source_path: str | None = None,
    ) -> "PlatformNameRegistry":
        normalized = {
            normalize_platform_name(name)
            for name in names
        }
        normalized.discard("")

        return cls(
            names=frozenset(normalized),
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

        if isinstance(document, Mapping):
            raw = document.get("platform_names")

        names = raw if isinstance(raw, list) else []
        registry = cls.from_names(names, source_path=text_path)

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
    "normalize_platform_name",
    "resolve_platform_names_path",
]
