"""Charset detection and decoding for Chinese web pages.

Precedence is explicit, highest first:

1. **BOM** (``utf-8-sig``, ``utf-16``) -- the payload states its own encoding.
2. **HTTP header** ``Content-Type; charset=...`` -- the transport's statement.
3. **``<meta charset>`` / ``<meta http-equiv="Content-Type">``** in the first
   bytes of the document -- the page's statement.
4. **``utf-8``** -- the default.

Legacy Chinese encodings are normalised: ``gb2312``/``gbk``/``x-gbk`` are decoded
with ``gb18030``, which is a superset and therefore cannot lose characters. A
label the runtime does not know falls back to ``utf-8`` with replacement instead
of raising, so one odd page never aborts a sync.
"""

from __future__ import annotations

import codecs
import re
from typing import Final

SNIFF_LIMIT_BYTES: Final[int] = 4096
DEFAULT_CHARSET: Final[str] = "utf-8"

_CHARSET_ALIASES = {
    "gb2312": "gb18030",
    "gbk": "gb18030",
    "x-gbk": "gb18030",
    "csgb2312": "gb18030",
    "gb_2312-80": "gb18030",
    "iso-8859-1": "latin-1",
    "latin1": "latin-1",
    "utf8": "utf-8",
    "utf-8-sig": "utf-8-sig",
}

_META_CHARSET_RE = re.compile(
    rb"""<meta[^>]+charset\s*=\s*["']?\s*([a-zA-Z0-9_\-:.]+)""",
    re.IGNORECASE,
)
_META_HTTP_EQUIV_RE = re.compile(
    rb"""<meta[^>]+content\s*=\s*["'][^"']*charset\s*=\s*([a-zA-Z0-9_\-:.]+)""",
    re.IGNORECASE,
)


def normalize_label(label: str | None) -> str | None:
    """Map an encoding label onto one the runtime can decode with."""

    if not isinstance(label, str):
        return None

    cleaned = label.strip().strip("\"'").lower()

    if not cleaned:
        return None

    if cleaned in _CHARSET_ALIASES:
        return _CHARSET_ALIASES[cleaned]

    try:
        codecs.lookup(cleaned)
    except LookupError:
        return None

    return cleaned


def charset_from_content_type(content_type: str | None) -> str | None:
    """Extract the charset from a ``Content-Type`` header value."""

    if not isinstance(content_type, str):
        return None

    for part in content_type.split(";"):
        key, separator, value = part.partition("=")

        if not separator:
            continue

        if key.strip().lower() == "charset":
            return normalize_label(value)

    return None


def charset_from_bom(payload: bytes) -> str | None:
    """Detect a UTF BOM; the BOM wins over every other signal."""

    if payload.startswith(codecs.BOM_UTF8):
        return "utf-8-sig"

    if payload.startswith(codecs.BOM_UTF32_LE) or payload.startswith(
        codecs.BOM_UTF32_BE
    ):
        return "utf-32"

    if payload.startswith(codecs.BOM_UTF16_LE) or payload.startswith(
        codecs.BOM_UTF16_BE
    ):
        return "utf-16"

    return None


def charset_from_meta(payload: bytes) -> str | None:
    """Detect ``<meta charset>`` in the head of a document."""

    head = payload[:SNIFF_LIMIT_BYTES]

    for pattern in (_META_CHARSET_RE, _META_HTTP_EQUIV_RE):
        match = pattern.search(head)

        if match is not None:
            label = normalize_label(
                match.group(1).decode("ascii", "ignore")
            )

            if label is not None:
                return label

    return None


def sniff_charset(
    payload: bytes,
    *,
    content_type: str | None = None,
) -> str:
    """Return the encoding to use, following the documented precedence."""

    if not isinstance(payload, (bytes, bytearray)):
        return DEFAULT_CHARSET

    data = bytes(payload)

    for candidate in (
        charset_from_bom(data),
        charset_from_content_type(content_type),
        charset_from_meta(data),
    ):
        if candidate is not None:
            return candidate

    return DEFAULT_CHARSET


def decode_body(
    payload: bytes,
    *,
    content_type: str | None = None,
) -> str:
    """Decode a payload; never raises, never guesses beyond the precedence."""

    if not isinstance(payload, (bytes, bytearray)):
        return ""

    data = bytes(payload)
    charset = sniff_charset(data, content_type=content_type)

    try:
        return data.decode(charset)
    except (UnicodeDecodeError, LookupError):
        try:
            return data.decode(charset, errors="replace")
        except LookupError:  # pragma: no cover - guarded by normalize_label
            return data.decode(DEFAULT_CHARSET, errors="replace")


def decode_html_entities(text: str) -> str:
    """Decode HTML entities without touching markup."""

    import html

    if not isinstance(text, str):
        return ""

    return html.unescape(text)
