"""ATS provider registry.

Every provider module exposes ``PROVIDER``, ``board_url(board)`` and
``parse_board(payload, *, board, company=None)``.
"""

from __future__ import annotations

from typing import Any

from . import ashby, greenhouse, lever, nowcoder  # registry-guard: source-id
from .base import BoardResult, ProviderError

PROVIDERS = {
    module.PROVIDER: module
    for module in (greenhouse, lever, ashby, nowcoder)  # registry-guard: source-id
}


def provider_module(name: str) -> Any:
    try:
        return PROVIDERS[name]
    except KeyError as error:
        raise ProviderError(
            f"unsupported provider '{name}'"
        ) from error


def normalize_board(
    name: str,
    payload: Any,
    *,
    board: str,
    company: str | None = None,
) -> BoardResult:
    """Dispatch payload normalization to the provider module."""

    return provider_module(name).parse_board(
        payload,
        board=board,
        company=company,
    )


__all__ = [
    "BoardResult",
    "PROVIDERS",
    "ProviderError",
    "normalize_board",
    "provider_module",
]
