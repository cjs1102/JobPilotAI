"""Configuration loading and validation for the source adapters.

Secrets never live in the config file: it only names the environment variable
that holds the internal API token (ExecPlan 0012 section 5).
"""

from __future__ import annotations

import os
from dataclasses import dataclass, field
from typing import Any, Mapping

import yaml

SUPPORTED_PROVIDERS = ("greenhouse", "lever", "ashby", "nowcoder")  # registry-guard: source-id

MAX_NOWCODER_BOARD_BYTES = 128
MAX_NOWCODER_KEYWORD_BYTES = 64
MAX_NOWCODER_CITY_BYTES = 32


class ConfigError(Exception):
    """Raised when the source adapter configuration is unusable."""


@dataclass(frozen=True)
class SourceConfig:
    type: str
    board: str
    enabled: bool = True
    company: str | None = None


@dataclass(frozen=True)
class JobPilotConfig:
    base_url: str
    internal_token_env: str
    sources: tuple[SourceConfig, ...] = field(default_factory=tuple)

    def enabled_sources(
        self,
        provider: str | None = None,
    ) -> tuple[SourceConfig, ...]:
        selected = [source for source in self.sources if source.enabled]

        if provider is not None:
            selected = [
                source
                for source in selected
                if source.type == provider
            ]

        return tuple(selected)


def _require_text(value: Any, label: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise ConfigError(f"{label} must be a non-empty string")

    return value.strip()


def _validate_board_spec(board: str, index: int) -> None:
    """Validate ``<keyword>`` or ``<keyword>|<city>`` for the campus source.

    The board string is also the ``source_scope``, so it must stay well inside
    the platform's 128-byte limit. The keyword may not be empty; a ``|`` means a
    city must follow it. Control characters are rejected so a YAML line cannot
    smuggle a URL or a header into the request path.
    """

    label = f"sources[{index}].board"

    if len(board.encode("utf-8")) > MAX_NOWCODER_BOARD_BYTES:
        raise ConfigError(
            f"{label} must be <= {MAX_NOWCODER_BOARD_BYTES} UTF-8 bytes"
        )

    if any(ord(character) < 32 for character in board):
        raise ConfigError(f"{label} must not contain control characters")

    parts = board.split("|")

    if len(parts) > 2:
        raise ConfigError(
            f"{label} must be '<keyword>' or '<keyword>|<city>'"
        )

    keyword = parts[0].strip()

    if not keyword:
        raise ConfigError(f"{label} requires a non-empty keyword")

    if len(keyword.encode("utf-8")) > MAX_NOWCODER_KEYWORD_BYTES:
        raise ConfigError(
            f"{label} keyword must be <= "
            f"{MAX_NOWCODER_KEYWORD_BYTES} UTF-8 bytes"
        )

    if len(parts) == 2:
        city = parts[1].strip()

        if not city:
            raise ConfigError(
                f"{label} has an empty city after '|'; omit the '|' instead"
            )

        if len(city.encode("utf-8")) > MAX_NOWCODER_CITY_BYTES:
            raise ConfigError(
                f"{label} city must be <= "
                f"{MAX_NOWCODER_CITY_BYTES} UTF-8 bytes"
            )


def _parse_source(raw: Any, index: int) -> SourceConfig:
    if not isinstance(raw, Mapping):
        raise ConfigError(f"sources[{index}] must be a mapping")

    source_type = _require_text(
        raw.get("type"),
        f"sources[{index}].type",
    )

    if source_type not in SUPPORTED_PROVIDERS:
        raise ConfigError(
            f"sources[{index}].type '{source_type}' is not supported; "
            f"expected one of {', '.join(SUPPORTED_PROVIDERS)}"
        )

    board = _require_text(
        raw.get("board"),
        f"sources[{index}].board",
    )

    if source_type == "nowcoder":  # registry-guard: source-id
        _validate_board_spec(board, index)

    enabled = raw.get("enabled", True)

    if not isinstance(enabled, bool):
        raise ConfigError(
            f"sources[{index}].enabled must be a boolean"
        )

    company = raw.get("company")

    if company is not None:
        company = _require_text(
            company,
            f"sources[{index}].company",
        )

    return SourceConfig(
        type=source_type,
        board=board,
        enabled=enabled,
        company=company,
    )


def load_config(path: str | os.PathLike[str]) -> JobPilotConfig:
    """Load and validate a YAML configuration file."""

    try:
        with open(path, "r", encoding="utf-8") as handle:
            document = yaml.safe_load(handle)
    except OSError as error:
        raise ConfigError(f"cannot read config: {error}") from error
    except yaml.YAMLError as error:
        raise ConfigError(f"invalid YAML config: {error}") from error

    if not isinstance(document, Mapping):
        raise ConfigError("config root must be a mapping")

    jobpilot = document.get("jobpilot")

    if not isinstance(jobpilot, Mapping):
        raise ConfigError("config requires a 'jobpilot' mapping")

    base_url = _require_text(
        jobpilot.get("base_url"),
        "jobpilot.base_url",
    )

    if not base_url.startswith(("http://", "https://")):
        raise ConfigError(
            "jobpilot.base_url must start with http:// or https://"
        )

    token_env = _require_text(
        jobpilot.get("internal_token_env"),
        "jobpilot.internal_token_env",
    )

    raw_sources = document.get("sources")

    if not isinstance(raw_sources, list) or not raw_sources:
        raise ConfigError("config requires a non-empty 'sources' list")

    sources = [
        _parse_source(raw, index)
        for index, raw in enumerate(raw_sources)
    ]

    seen: set[tuple[str, str]] = set()

    for source in sources:
        key = (source.type, source.board)

        if key in seen:
            raise ConfigError(
                f"duplicate source ({source.type}, {source.board})"
            )

        seen.add(key)

    return JobPilotConfig(
        base_url=base_url.rstrip("/"),
        internal_token_env=token_env,
        sources=tuple(sources),
    )


def resolve_internal_token(
    config: JobPilotConfig,
    environ: Mapping[str, str] | None = None,
) -> str:
    """Read the internal token before any network call happens."""

    env = os.environ if environ is None else environ
    token = env.get(config.internal_token_env)

    if not isinstance(token, str) or not token.strip():
        raise ConfigError(
            f"environment variable {config.internal_token_env} "
            "must hold the internal API token"
        )

    return token.strip()
