"""Defensive HTML extraction: JSON-LD, embedded JSON, links and metadata.

No third-party parser is used (the package may only depend on PyYAML), so the
extractors are built on :mod:`html.parser` and are deliberately forgiving: a
malformed block is skipped and counted, never allowed to raise, because a shape
change must degrade to ``complete=false`` -- not abort a run with a traceback.
Exactly one behaviour is strict: a missing *required* container is signalled to
the caller by an empty result, and the provider decides it cannot prove
completeness.
"""

from __future__ import annotations

import json
import re
import urllib.parse
from dataclasses import dataclass, field
from html.parser import HTMLParser
from typing import Any, Mapping

from .normalizer import clean_text

_SCRIPT_TYPE_RE = re.compile(
    r"""<script[^>]*type\s*=\s*["']([^"']+)["'][^>]*>""",
    re.IGNORECASE,
)
_JSON_LD_TYPES = ("application/ld+json", "application/json+ld")


@dataclass
class JsonLdResult:
    blocks: list[Any] = field(default_factory=list)
    skipped: int = 0


@dataclass
class LinkResult:
    links: list[tuple[str, str]] = field(default_factory=list)
    skipped: int = 0


class _ScriptCollector(HTMLParser):
    """Collect ``<script>`` bodies together with their declared type."""

    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.scripts: list[tuple[str, str]] = []
        self._active_type: str | None = None
        self._buffer: list[str] = []

    def handle_starttag(
        self, tag: str, attrs: list[tuple[str, str | None]]
    ) -> None:
        if tag.lower() != "script":
            return

        attributes = {
            key.lower(): (value or "") for key, value in attrs
        }
        self._active_type = attributes.get("type", "").strip().lower()
        self._buffer = []

    def handle_data(self, data: str) -> None:
        if self._active_type is not None:
            self._buffer.append(data)

    def handle_endtag(self, tag: str) -> None:
        if tag.lower() != "script" or self._active_type is None:
            return

        self.scripts.append((self._active_type, "".join(self._buffer)))
        self._active_type = None
        self._buffer = []


class _DocumentCollector(HTMLParser):
    """Collect links, metadata and the ``<base href>`` value."""

    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.links: list[tuple[str, str]] = []
        self.meta: dict[str, str] = {}
        self.base_href: str | None = None
        self._active_link: str | None = None
        self._link_text: list[str] = []

    def handle_starttag(
        self, tag: str, attrs: list[tuple[str, str | None]]
    ) -> None:
        name = tag.lower()
        attributes = {
            key.lower(): (value or "") for key, value in attrs
        }

        if name == "a":
            self._active_link = attributes.get("href")
            self._link_text = []
        elif name == "base" and self.base_href is None:
            href = attributes.get("href")

            if href:
                self.base_href = href
        elif name == "meta":
            key = (
                attributes.get("property")
                or attributes.get("name")
                or attributes.get("itemprop")
                or ""
            ).strip().lower()
            content = attributes.get("content")

            if key and content and key not in self.meta:
                self.meta[key] = content

    def handle_data(self, data: str) -> None:
        if self._active_link is not None:
            self._link_text.append(data)

    def handle_endtag(self, tag: str) -> None:
        if tag.lower() != "a" or self._active_link is None:
            return

        text = clean_text(" ".join(self._link_text)) or ""
        self.links.append((text, self._active_link))
        self._active_link = None
        self._link_text = []


def extract_json_ld(html_text: object) -> JsonLdResult:
    """Return every JSON-LD block that parses, counting the ones that do not."""

    result = JsonLdResult()

    if not isinstance(html_text, str) or not html_text:
        return result

    for script_type, body in _script_payloads(html_text):
        if script_type not in _JSON_LD_TYPES:
            continue

        stripped = body.strip()

        if not stripped:
            result.skipped += 1
            continue

        try:
            result.blocks.append(json.loads(stripped))
        except (json.JSONDecodeError, ValueError):
            result.skipped += 1

    return result


def find_job_postings(html_text: object) -> tuple[list[dict[str, Any]], int]:
    """Extract JSON-LD ``JobPosting`` objects (including ``@graph`` members).

    The second value counts blocks that were dropped: JSON-LD that failed to
    parse plus structurally unusable documents.
    """

    result = extract_json_ld(html_text)
    postings: list[dict[str, Any]] = []
    skipped = result.skipped

    for block in result.blocks:
        skipped += extract_postings_from_block(block, postings)

    return postings, skipped


def extract_postings_from_block(
    block: Any,
    postings: list[dict[str, Any]],
) -> int:
    """Append ``JobPosting`` items found in one JSON-LD document."""

    skipped = 0

    if isinstance(block, list):
        for item in block:
            skipped += extract_postings_from_block(item, postings)

        return skipped

    if not isinstance(block, Mapping):
        return 1

    graph = block.get("@graph")

    if isinstance(graph, list):
        for item in graph:
            skipped += extract_postings_from_block(item, postings)

        return skipped

    block_type = block.get("@type")

    if isinstance(block_type, list):
        types = [str(value) for value in block_type]
    elif isinstance(block_type, str):
        types = [block_type]
    else:
        types = []

    if any(value.lower() == "jobposting" for value in types):
        postings.append(dict(block))
        return 0

    # A container document without JobPosting is not an error, but it also did
    # not supply anything usable.
    return 0


def extract_links(
    html_text: object,
    *,
    base_url: str | None = None,
) -> LinkResult:
    """Return ``(text, absolute_url)`` pairs for http(s) links only."""

    result = LinkResult()

    if not isinstance(html_text, str) or not html_text:
        return result

    parser = _DocumentCollector()

    if not _feed(parser, html_text):
        return result

    base = base_url
    collected = parser.base_href

    if collected and base_url:
        base = urllib.parse.urljoin(base_url, collected)

    for text, href in parser.links:
        resolved = resolve_link(href, base_url=base)

        if resolved is None:
            result.skipped += 1
            continue

        result.links.append((text, resolved))

    return result


def extract_meta(html_text: object) -> dict[str, str]:
    """Return ``meta`` name/property -> content (first value wins)."""

    if not isinstance(html_text, str) or not html_text:
        return {}

    parser = _DocumentCollector()

    if not _feed(parser, html_text):
        return {}

    return dict(parser.meta)


def resolve_link(href: object, *, base_url: str | None) -> str | None:
    """Resolve a link and keep only usable http(s) targets."""

    if not isinstance(href, str) or not href.strip():
        return None

    candidate = href.strip()

    if candidate.startswith(("javascript:", "mailto:", "tel:", "#")):
        return None

    if base_url:
        candidate = urllib.parse.urljoin(base_url, candidate)

    parts = urllib.parse.urlsplit(candidate)

    if parts.scheme not in {"http", "https"} or not parts.netloc:
        return None

    return urllib.parse.urlunsplit(
        (parts.scheme, parts.netloc, parts.path, parts.query, "")
    )


def _script_payloads(html_text: str) -> list[tuple[str, str]]:
    parser = _ScriptCollector()

    if not _feed(parser, html_text):
        # Flat fallback keeps JSON-LD extraction working on fragments that a
        # real parser rejects.
        return _flat_script_payloads(html_text)

    return parser.scripts


def _flat_script_payloads(html_text: str) -> list[tuple[str, str]]:
    payloads: list[tuple[str, str]] = []
    position = 0

    while True:
        match = _SCRIPT_TYPE_RE.search(html_text, position)

        if match is None:
            break

        end = html_text.find("</script>", match.end())

        if end < 0:
            break

        payloads.append(
            (
                match.group(1).strip().lower(),
                html_text[match.end() : end],
            )
        )
        position = end + len("</script>")

    return payloads


def _feed(parser: HTMLParser, html_text: str) -> bool:
    try:
        parser.feed(html_text)
        parser.close()
    except Exception:  # noqa: BLE001 - malformed HTML is expected input
        return False

    return True


def json_ld_to_job_fields(posting: Mapping[str, Any]) -> dict[str, Any]:
    """Map one JSON-LD ``JobPosting`` onto raw source fields.

    Only structurally present fields are returned; no value is derived from
    free text here (that belongs to :mod:`jobpilot_sources.normalize_cn`).
    """

    fields: dict[str, Any] = {}

    for key in ("title", "description", "datePosted", "validThrough"):
        value = posting.get(key)

        if isinstance(value, str) and value.strip():
            fields[key] = value.strip()

    identifier = posting.get("identifier")

    if isinstance(identifier, Mapping):
        value = identifier.get("value")

        if isinstance(value, (str, int)) and not isinstance(value, bool):
            fields["identifier"] = str(value)
    elif isinstance(identifier, (str, int)) and not isinstance(identifier, bool):
        fields["identifier"] = str(identifier)

    url = posting.get("url")

    if isinstance(url, str) and url.strip():
        fields["url"] = url.strip()

    organization = posting.get("hiringOrganization")

    if isinstance(organization, Mapping):
        name = organization.get("name")

        if isinstance(name, str) and name.strip():
            fields["company"] = name.strip()

    location = posting.get("jobLocation")

    if isinstance(location, list) and location:
        location = location[0]

    if isinstance(location, Mapping):
        address = location.get("address")

        if isinstance(address, Mapping):
            parts = [
                value.strip()
                for value in (
                    address.get("addressLocality"),
                    address.get("addressRegion"),
                )
                if isinstance(value, str) and value.strip()
            ]

            if parts:
                fields["location"] = parts[0]

    return fields


__all__ = [
    "JsonLdResult",
    "LinkResult",
    "extract_json_ld",
    "extract_links",
    "extract_meta",
    "extract_postings_from_block",
    "find_job_postings",
    "json_ld_to_job_fields",
    "resolve_link",
]
