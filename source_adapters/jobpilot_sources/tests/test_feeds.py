"""Offline tests for RSS/Atom/sitemap parsing and its XML safety rules."""

from __future__ import annotations

import unittest

from jobpilot_sources.feeds import (
    FeedError,
    looks_like_feed,
    looks_like_sitemap,
    parse_feed,
    parse_rfc822_utc,
    parse_sitemap,
)
from jobpilot_sources.tests.support import text_fixture


class FeedTest(unittest.TestCase):
    def test_rss_entries_are_parsed(self) -> None:
        result = parse_feed(text_fixture("cn_feed.xml"))

        self.assertTrue(result.recognized)
        self.assertEqual(len(result.entries), 3)
        first = result.entries[0]

        self.assertEqual(first.title, "2027 届校园招聘启动")
        self.assertEqual(
            first.link, "https://jobs.example-cn.invalid/campus/3001"
        )
        self.assertEqual(first.published, "2026-10-08 04:00:00")
        self.assertEqual(first.summary, "共 120 个岗位")

    def test_rss_offset_dates_are_converted_to_utc(self) -> None:
        result = parse_feed(text_fixture("cn_feed.xml"))

        self.assertEqual(result.entries[1].published, "2026-10-09 04:30:00")

    def test_entry_without_link_keeps_an_empty_link(self) -> None:
        result = parse_feed(text_fixture("cn_feed.xml"))

        self.assertIsNone(result.entries[2].link)

    def test_atom_entries_are_parsed(self) -> None:
        atom = (
            '<?xml version="1.0"?>'
            '<feed xmlns="http://www.w3.org/2005/Atom">'
            "<entry><title>B</title><id>x2</id>"
            '<link rel="alternate" href="https://y/2"/>'
            "<updated>2026-10-08T12:00:00+08:00</updated>"
            "<summary>摘要</summary></entry></feed>"
        )

        result = parse_feed(atom)

        self.assertTrue(result.recognized)
        entry = result.entries[0]
        self.assertEqual(entry.title, "B")
        self.assertEqual(entry.link, "https://y/2")
        self.assertEqual(entry.identifier, "x2")
        self.assertEqual(entry.published, "2026-10-08 04:00:00")
        self.assertEqual(entry.summary, "摘要")

    def test_unknown_document_is_not_recognized(self) -> None:
        result = parse_feed("<html><body>x</body></html>")

        self.assertFalse(result.recognized)
        self.assertEqual(result.entries, [])

    def test_malformed_xml_returns_empty(self) -> None:
        self.assertEqual(parse_feed("<rss><channel>").entries, [])
        self.assertEqual(parse_feed("").entries, [])
        self.assertEqual(parse_feed(None).entries, [])

    def test_doctype_and_entity_declarations_are_refused(self) -> None:
        for payload in (
            '<?xml version="1.0"?><!DOCTYPE rss [<!ENTITY a "b">]><rss/>',
            '<?xml version="1.0"?><!ENTITY x "y"><rss/>',
        ):
            with self.subTest(payload=payload):
                with self.assertRaises(FeedError):
                    parse_feed(payload)

    def test_rfc822_requires_a_zone(self) -> None:
        self.assertEqual(
            parse_rfc822_utc("Wed, 08 Oct 2026 04:00:00 GMT"),
            "2026-10-08 04:00:00",
        )
        self.assertIsNone(parse_rfc822_utc("Wed, 08 Oct 2026 04:00:00"))
        self.assertIsNone(parse_rfc822_utc("not a date"))
        self.assertIsNone(parse_rfc822_utc(None))

    def test_sniffers(self) -> None:
        self.assertTrue(looks_like_feed(text_fixture("cn_feed.xml")))
        self.assertFalse(looks_like_feed("<html>"))
        self.assertTrue(looks_like_sitemap(text_fixture("cn_sitemap.xml")))
        self.assertFalse(looks_like_sitemap("<html>"))


class SitemapTest(unittest.TestCase):
    def test_urlset_locations_are_returned(self) -> None:
        result = parse_sitemap(text_fixture("cn_sitemap.xml"))

        self.assertTrue(result.recognized)
        self.assertFalse(result.is_index)
        self.assertEqual(
            result.urls,
            [
                "https://jobs.example-cn.invalid/campus?page=1",
                "https://jobs.example-cn.invalid/campus?page=2",
            ],
        )

    def test_sitemap_index_is_detected(self) -> None:
        index = (
            '<?xml version="1.0"?>'
            '<sitemapindex xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">'
            "<sitemap><loc>https://x/sitemap-1.xml</loc></sitemap>"
            "</sitemapindex>"
        )

        result = parse_sitemap(index)

        self.assertTrue(result.is_index)
        self.assertEqual(result.urls, ["https://x/sitemap-1.xml"])

    def test_malformed_or_unknown_documents(self) -> None:
        self.assertEqual(parse_sitemap("<urlset>").urls, [])
        self.assertFalse(parse_sitemap("<html/>").recognized)


if __name__ == "__main__":
    unittest.main()
