"""Shared provider types (kept separate to avoid import cycles)."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any


class ProviderError(Exception):
    """Raised when a provider payload does not match the expected shape.

    A rejection that maps onto a stable 0015A quality code carries it here
    (``code``, ``field`` and, for a byte cap, ``limit_bytes``/``actual_bytes``),
    mirroring :class:`jobpilot_sources.capture.artifact.CaptureError`, so a
    caller can report the same machine-readable diagnostic the C++ gate would.
    Plain shape errors leave all four as ``None``.
    """

    def __init__(
        self,
        message: str,
        *,
        code: str | None = None,
        field: str | None = None,
        limit_bytes: int | None = None,
        actual_bytes: int | None = None,
    ) -> None:
        super().__init__(message)
        self.code = code
        self.field = field
        self.limit_bytes = limit_bytes
        self.actual_bytes = actual_bytes


@dataclass(frozen=True)
class BoardResult:
    source: str
    board: str
    jobs: list[dict[str, Any]] = field(default_factory=list)
    complete: bool = False
    total: int | None = None
    #: Row-level skips/diagnostics. Providers that cannot parse a row keep the
    #: rest of the board and report a stable ``reason`` here instead of failing
    #: the whole observation. Existing providers leave it empty.
    diagnostics: list[dict[str, Any]] = field(default_factory=list)
