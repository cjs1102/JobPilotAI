"""RSS/Atom feed and sitemap parsing with stdlib ``xml.etree``.

Safety: the XML parser refuses documents that declare a ``DOCTYPE`` or an
``ENTITY``. Entity expansion ("billion laughs") and external entity loading are
the two classic XML attacks, ``defusedxml`` is not an allowed dependency, and
this package never needs a DTD -- so the declaration is rejected outright rather
than parsed defensively. Malformed XML returns an empty result: a feed that
cannot be parsed must degrade to ``complete=false``, never crash the run.
"""

from __future__ import annotations

import email.utils
import re
import xml.etree.ElementTree as ElementTree
from dataclasses import dataclass, field
from datetime import timezone
from typing import Any

from .normalizer import clean_text, html_to_text
from .timestamps import DB_TIMESTAMP_FORMAT, normalize_timestamp

_DOCTYPE_RE = re.compile(r"<!\s*(DOCTYPE|ENTITY)", re.IGNORECASE)
_WHITESPACE_RE = re.compile(r"\s+")


class FeedError(Exception):
    """Raised only for an explicitly refused document (DOCTYPE/ENTITY)."""


@dataclass
class FeedEntry:
    title: str | None = None
    link: str | None = None
    identifier: str | None = None
    published: str | None = None
    summary: str | None = None
    raw: dict[str, Any] = field(default_factory=dict)


@dataclass
class FeedResult:
    entries: list[FeedEntry] = field(default_factory=list)
    #: True when the document declared itself a feed/index we understood.
    recognized: bool = False


@dataclass
class SitemapResult:
    urls: list[str] = field(default_factory=list)
    is_index: bool = False
    recognized: bool = False


def parse_feed(xml_text: object) -> FeedResult:
    """Parse RSS 2.0 or Atom into entries (order preserved)."""

    root = _parse_xml(xml_text)

    if root is None:
        return FeedResult()

    tag = _local_name(root.tag)

    if tag == "rss":
        channel = _first_child(root, "channel")

        if channel is None:
            return FeedResult(recognized=True)

        entries = [
            _rss_entry(item)
            for item in _children(channel, "item")
        ]

        return FeedResult(entries=entries, recognized=True)

    if tag == "feed":
        entries = [
            _atom_entry(entry)
            for entry in _children(root, "entry")
        ]

        return FeedResult(entries=entries, recognized=True)

    if tag == "rdf":
        # RSS 1.0 (RDF). Items are siblings of <channel> under <rdf:RDF>.
        entries = [
            _rss_entry(item)
            for item in _children(root, "item")
        ]

        return FeedResult(entries=entries, recognized=True)

    return FeedResult()


def parse_sitemap(xml_text: object) -> SitemapResult:
    """Parse ``urlset`` or ``sitemapindex`` and return the ``<loc>`` values."""

    root = _parse_xml(xml_text)

    if root is None:
        return SitemapResult()

    tag = _local_name(root.tag)

    if tag == "urlset":
        return SitemapResult(
            urls=[
                loc
                for loc in (
                    _text(_first_child(url, "loc"))
                    for url in _children(root, "url")
                )
                if loc
            ],
            recognized=True,
        )

    if tag == "sitemapindex":
        return SitemapResult(
            urls=[
                loc
                for loc in (
                    _text(_first_child(sitemap, "loc"))
                    for sitemap in _children(root, "sitemap")
                )
                if loc
            ],
            is_index=True,
            recognized=True,
        )

    return SitemapResult()


def looks_like_feed(text: object) -> bool:
    """Cheap content sniff so a caller can route HTML vs feed vs sitemap."""

    if not isinstance(text, str):
        return False

    head = text[:2048].lower()
    return "<rss" in head or "<feed" in head or "<rdf" in head


def looks_like_sitemap(text: object) -> bool:
    if not isinstance(text, str):
        return False

    head = text[:2048].lower()
    return "<urlset" in head or "<sitemapindex" in head


def _rss_entry(item: ElementTree.Element) -> FeedEntry:
    title = _text(_first_child(item, "title"))
    link = _text(_first_child(item, "link"))
    guid = _text(_first_child(item, "guid"))
    pub_date = _text(_first_child(item, "pubDate"))
    description = _text(_first_child(item, "description"))

    published = parse_rfc822_utc(pub_date) or _normalize_iso(pub_date)

    entry = FeedEntry(
        title=title,
        link=link,
        identifier=guid or link,
        published=published,
        summary=html_to_text(description) if description else None,
    )
    entry.raw = {"pubDate": pub_date}
    return entry


def _atom_entry(item: ElementTree.Element) -> FeedEntry:
    title = _text(_first_child(item, "title"))
    identifier = _text(_first_child(item, "id"))
    published = _text(_first_child(item, "published")) or _text(
        _first_child(item, "updated")
    )
    summary = _text(_first_child(item, "summary")) or _text(
        _first_child(item, "content")
    )

    link = None

    for candidate in _children(item, "link"):
        rel = candidate.attrib.get("rel", "alternate")
        href = candidate.attrib.get("href")

        if href and rel in {"alternate", ""}:
            link = href.strip()
            break

    entry = FeedEntry(
        title=title,
        link=link,
        identifier=identifier or link,
        published=_normalize_iso(published),
        summary=html_to_text(summary) if summary else None,
    )
    entry.raw = {"updated": published}
    return entry


def parse_rfc822_utc(value: object) -> str | None:
    """Parse an RFC-822/1123 ``pubDate`` into UTC text."""

    if not isinstance(value, str) or not value.strip():
        return None

    try:
        parsed = email.utils.parsedate_to_datetime(value.strip())
    except (TypeError, ValueError):
        return None

    if parsed is None:
        return None

    if parsed.tzinfo is None:
        # A feed without a zone states no absolute instant; the field is
        # omitted rather than guessed (Beijing noon would be a fabricated
        # instant for a publication time).
        return None

    return parsed.astimezone(timezone.utc).strftime(DB_TIMESTAMP_FORMAT)


def _normalize_iso(value: str | None) -> str | None:
    if value is None:
        return None

    normalized = normalize_timestamp(value)

    return None if normalized is None else normalized.utc


def _parse_xml(xml_text: object) -> ElementTree.Element | None:
    if not isinstance(xml_text, str) or not xml_text.strip():
        return None

    if _DOCTYPE_RE.search(xml_text):
        raise FeedError("XML DOCTYPE/ENTITY declarations are refused")

    try:
        return ElementTree.fromstring(xml_text)
    except ElementTree.ParseError:
        return None


def _local_name(tag: Any) -> str:
    if not isinstance(tag, str):
        return ""

    return tag.rsplit("}", 1)[-1].lower()


def _children(
    element: ElementTree.Element,
    name: str,
) -> list[ElementTree.Element]:
    wanted = name.lower()

    return [
        child
        for child in list(element)
        if _local_name(child.tag) == wanted
    ]


def _first_child(
    element: ElementTree.Element,
    name: str,
) -> ElementTree.Element | None:
    wanted = name.lower()

    for child in list(element):
        if _local_name(child.tag) == wanted:
            return child

    return None


def _text(element: ElementTree.Element | None) -> str | None:
    if element is None:
        return None

    return clean_text(
        _WHITESPACE_RE.sub(" ", "".join(element.itertext()))
    )


__all__ = [
    "FeedEntry",
    "FeedError",
    "FeedResult",
    "SitemapResult",
    "looks_like_feed",
    "looks_like_sitemap",
    "parse_feed",
    "parse_rfc822_utc",
    "parse_sitemap",
]
