"""Configuration loading and validation for the AI worker.

Secrets never live in the config file: it only names the environment variables
that hold the internal API token and the provider key (ExecPlan 0013 sections
6.2 and 6.4). A provider section that tries to carry a key directly is rejected
by name so a secret cannot be committed by accident.

The provider ``base_url`` is validated here: loopback, private, link-local and
metadata addresses are refused unless ``provider.allow_private_endpoint`` is
explicitly true (ExecPlan 0013 section 17 amendment 2).
"""

from __future__ import annotations

import ipaddress
import os
from dataclasses import dataclass
from typing import Any, Mapping
from urllib.parse import urlsplit

import yaml

DEFAULT_WORKER_ID = "jobpilot-ai-worker"
DEFAULT_LEASE_SECONDS = 120
DEFAULT_INTERVAL_SECONDS = 5.0
DEFAULT_MAX_ATTEMPTS = 3
DEFAULT_TASK_TYPES = ("job_match",)
DEFAULT_REQUEST_TIMEOUT_SECONDS = 15.0
DEFAULT_MAX_RETRIES = 1
DEFAULT_PROVIDER_TIMEOUT_SECONDS = 30.0

# Field names that would put a secret in the config file. Rejected explicitly.
SECRET_FIELD_NAMES = (
    "key",
    "api_key",
    "apikey",
    "access_key",
    "token",
    "secret",
    "password",
    "passwd",
    "credential",
    "credentials",
)

BLOCKED_HOST_NAMES = frozenset(
    {
        "localhost",
        "localhost.localdomain",
        "ip6-localhost",
        "ip6-loopback",
        "metadata",
        "metadata.google.internal",
        "metadata.goog",
    }
)

_CGNAT = ipaddress.IPv4Network("100.64.0.0/10")

_JOBPILOT_KEYS = frozenset(
    {
        "base_url",
        "internal_token_env",
        "worker_id",
        "lease_seconds",
        "interval_seconds",
        "max_attempts",
        "task_types",
        "request_timeout_seconds",
        "max_retries",
    }
)

_PROVIDER_KEYS = frozenset(
    {
        "base_url",
        "model",
        "key_env",
        "embedding_model",
        "timeout_seconds",
        "max_retries",
        "allow_private_endpoint",
    }
)

_ROOT_KEYS = frozenset({"jobpilot", "provider"})


class ConfigError(Exception):
    """Raised when the worker configuration is unusable."""


@dataclass(frozen=True)
class ProviderSettings:
    """Provider-neutral OpenAI-compatible endpoint settings."""

    base_url: str
    model: str
    key_env: str | None = None
    embedding_model: str | None = None
    timeout_seconds: float = DEFAULT_PROVIDER_TIMEOUT_SECONDS
    max_retries: int = DEFAULT_MAX_RETRIES
    allow_private_endpoint: bool = False


@dataclass(frozen=True)
class WorkerConfig:
    base_url: str
    internal_token_env: str
    worker_id: str = DEFAULT_WORKER_ID
    lease_seconds: int = DEFAULT_LEASE_SECONDS
    interval_seconds: float = DEFAULT_INTERVAL_SECONDS
    max_attempts: int = DEFAULT_MAX_ATTEMPTS
    task_types: tuple[str, ...] = DEFAULT_TASK_TYPES
    request_timeout_seconds: float = DEFAULT_REQUEST_TIMEOUT_SECONDS
    max_retries: int = DEFAULT_MAX_RETRIES
    provider: ProviderSettings | None = None


def _require_text(value: Any, label: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise ConfigError(f"{label} must be a non-empty string")

    return value.strip()


def _optional_text(value: Any, label: str) -> str | None:
    if value is None:
        return None

    return _require_text(value, label)


def _require_int(value: Any, label: str, *, minimum: int) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise ConfigError(f"{label} must be an integer")

    if value < minimum:
        raise ConfigError(f"{label} must be >= {minimum}")

    return value


def _require_number(value: Any, label: str, *, minimum: float) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ConfigError(f"{label} must be a number")

    number = float(value)

    if number < minimum:
        raise ConfigError(f"{label} must be >= {minimum:g}")

    return number


def _require_bool(value: Any, label: str) -> bool:
    if not isinstance(value, bool):
        raise ConfigError(f"{label} must be a boolean")

    return value


def _reject_unknown(
    raw: Mapping[str, Any],
    allowed: frozenset[str],
    label: str,
) -> None:
    unknown = sorted(str(key) for key in raw if key not in allowed)

    if unknown:
        raise ConfigError(
            f"{label} contains unsupported field(s): {', '.join(unknown)}"
        )


def _address_kind(
    address: ipaddress.IPv4Address | ipaddress.IPv6Address,
) -> str | None:
    """Classify an address that must not be reached implicitly."""

    mapped = getattr(address, "ipv4_mapped", None)

    if mapped is not None:
        return _address_kind(mapped)

    if address.is_loopback:
        return "loopback"

    if address.is_link_local:
        return "link-local"

    if address.is_multicast:
        return "multicast"

    if address.is_unspecified:
        return "unspecified"

    if address.is_private or (
        isinstance(address, ipaddress.IPv4Address) and address in _CGNAT
    ):
        return "private"

    if address.is_reserved:
        return "reserved"

    return None


def validate_provider_base_url(
    base_url: str,
    *,
    allow_private_endpoint: bool = False,
) -> str:
    """Validate a provider base URL, refusing loopback/private/metadata hosts.

    Returns the normalized URL (no trailing slash). A hostname is only checked
    literally: no DNS resolution happens here, so a public hostname that
    resolves to a private address is not caught (documented limitation).
    """

    text = _require_text(base_url, "provider.base_url")
    parsed = urlsplit(text)

    if parsed.scheme not in ("http", "https"):
        raise ConfigError(
            "provider.base_url must start with http:// or https://"
        )

    if parsed.username or parsed.password:
        raise ConfigError(
            "provider.base_url must not embed credentials; "
            "use provider.key_env instead"
        )

    if parsed.query or parsed.fragment:
        raise ConfigError(
            "provider.base_url must not contain a query string or fragment"
        )

    try:
        host = parsed.hostname
        parsed.port
    except ValueError as error:
        raise ConfigError(f"provider.base_url has an invalid port: {error}")

    if not host:
        raise ConfigError("provider.base_url must include a host")

    if host.casefold() in BLOCKED_HOST_NAMES:
        if not allow_private_endpoint:
            raise ConfigError(
                f"provider.base_url host '{host}' is a loopback/metadata name; "
                "set provider.allow_private_endpoint: true to use a local "
                "provider server"
            )
        return text.rstrip("/")

    try:
        address = ipaddress.ip_address(host)
    except ValueError:
        address = None

    if address is not None:
        kind = _address_kind(address)

        if kind is not None and not allow_private_endpoint:
            raise ConfigError(
                f"provider.base_url host '{host}' is a {kind} address; "
                "set provider.allow_private_endpoint: true to use a local "
                "provider server"
            )

    return text.rstrip("/")


def _parse_jobpilot(raw: Any) -> dict[str, Any]:
    if not isinstance(raw, Mapping):
        raise ConfigError("config requires a 'jobpilot' mapping")

    _reject_unknown(raw, _JOBPILOT_KEYS, "jobpilot")

    base_url = _require_text(raw.get("base_url"), "jobpilot.base_url")

    if not base_url.startswith(("http://", "https://")):
        raise ConfigError(
            "jobpilot.base_url must start with http:// or https://"
        )

    token_env = _require_text(
        raw.get("internal_token_env"),
        "jobpilot.internal_token_env",
    )

    worker_id = _optional_text(raw.get("worker_id"), "jobpilot.worker_id")
    lease_seconds = _require_int(
        raw.get("lease_seconds", DEFAULT_LEASE_SECONDS),
        "jobpilot.lease_seconds",
        minimum=1,
    )
    interval_seconds = _require_number(
        raw.get("interval_seconds", DEFAULT_INTERVAL_SECONDS),
        "jobpilot.interval_seconds",
        minimum=0.0,
    )
    max_attempts = _require_int(
        raw.get("max_attempts", DEFAULT_MAX_ATTEMPTS),
        "jobpilot.max_attempts",
        minimum=1,
    )
    request_timeout = _require_number(
        raw.get("request_timeout_seconds", DEFAULT_REQUEST_TIMEOUT_SECONDS),
        "jobpilot.request_timeout_seconds",
        minimum=0.001,
    )
    max_retries = _require_int(
        raw.get("max_retries", DEFAULT_MAX_RETRIES),
        "jobpilot.max_retries",
        minimum=0,
    )

    raw_types = raw.get("task_types", list(DEFAULT_TASK_TYPES))

    if not isinstance(raw_types, list) or not raw_types:
        raise ConfigError(
            "jobpilot.task_types must be a non-empty list of strings"
        )

    task_types: list[str] = []

    for index, item in enumerate(raw_types):
        task_types.append(
            _require_text(item, f"jobpilot.task_types[{index}]")
        )

    return {
        "base_url": base_url.rstrip("/"),
        "internal_token_env": token_env,
        "worker_id": worker_id or DEFAULT_WORKER_ID,
        "lease_seconds": lease_seconds,
        "interval_seconds": interval_seconds,
        "max_attempts": max_attempts,
        "task_types": tuple(task_types),
        "request_timeout_seconds": request_timeout,
        "max_retries": max_retries,
    }


def _parse_provider(raw: Any) -> ProviderSettings:
    if not isinstance(raw, Mapping):
        raise ConfigError("provider must be a mapping")

    for field in SECRET_FIELD_NAMES:
        if field in raw:
            raise ConfigError(
                f"provider.{field} is not supported; the key is read from the "
                "environment variable named by provider.key_env and never from "
                "the config file"
            )

    _reject_unknown(raw, _PROVIDER_KEYS, "provider")

    allow_private = _require_bool(
        raw.get("allow_private_endpoint", False),
        "provider.allow_private_endpoint",
    )
    base_url = validate_provider_base_url(
        _require_text(raw.get("base_url"), "provider.base_url"),
        allow_private_endpoint=allow_private,
    )
    model = _require_text(raw.get("model"), "provider.model")
    key_env = _optional_text(raw.get("key_env"), "provider.key_env")
    embedding_model = _optional_text(
        raw.get("embedding_model"),
        "provider.embedding_model",
    )
    timeout_seconds = _require_number(
        raw.get("timeout_seconds", DEFAULT_PROVIDER_TIMEOUT_SECONDS),
        "provider.timeout_seconds",
        minimum=0.001,
    )
    max_retries = _require_int(
        raw.get("max_retries", DEFAULT_MAX_RETRIES),
        "provider.max_retries",
        minimum=0,
    )

    return ProviderSettings(
        base_url=base_url,
        model=model,
        key_env=key_env,
        embedding_model=embedding_model,
        timeout_seconds=timeout_seconds,
        max_retries=max_retries,
        allow_private_endpoint=allow_private,
    )


def parse_config(document: Any) -> WorkerConfig:
    """Validate an already-loaded configuration document."""

    if not isinstance(document, Mapping):
        raise ConfigError("config root must be a mapping")

    _reject_unknown(document, _ROOT_KEYS, "config")

    jobpilot = _parse_jobpilot(document.get("jobpilot"))
    raw_provider = document.get("provider")
    provider = None if raw_provider is None else _parse_provider(raw_provider)

    return WorkerConfig(provider=provider, **jobpilot)


def load_config(path: str | os.PathLike[str]) -> WorkerConfig:
    """Load and validate a YAML configuration file."""

    try:
        with open(path, "r", encoding="utf-8") as handle:
            document = yaml.safe_load(handle)
    except OSError as error:
        raise ConfigError(f"cannot read config: {error}") from error
    except yaml.YAMLError as error:
        raise ConfigError(f"invalid YAML config: {error}") from error

    if document is None:
        raise ConfigError("config file is empty")

    return parse_config(document)


def resolve_internal_token(
    config: WorkerConfig,
    environ: Mapping[str, str] | None = None,
) -> str:
    """Read the internal token before any network call happens."""

    env = os.environ if environ is None else environ
    token = env.get(config.internal_token_env)

    if not isinstance(token, str) or not token.strip():
        raise ConfigError(
            f"environment variable {config.internal_token_env} must hold the "
            "internal API token"
        )

    return token.strip()
