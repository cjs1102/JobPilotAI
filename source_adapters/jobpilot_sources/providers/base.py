"""Shared provider types (kept separate to avoid import cycles)."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any


class ProviderError(Exception):
    """Raised when a provider payload does not match the expected shape."""


@dataclass(frozen=True)
class BoardResult:
    source: str
    board: str
    jobs: list[dict[str, Any]] = field(default_factory=list)
    complete: bool = False
    total: int | None = None
