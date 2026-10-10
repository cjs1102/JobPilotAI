"""Offline tests for JSON-LD / HTML / link extraction (defensive parsing)."""

from __future__ import annotations

import unittest

from jobpilot_sources.htmljob import (
    extract_json_ld,
    extract_links,
    extract_meta,
    find_job_postings,
    json_ld_to_job_fields,
    resolve_link,
)
from jobpilot_sources.tests.support import text_fixture


class JsonLdTest(unittest.TestCase):
    def test_extracts_graph_postings_and_counts_broken_blocks(self) -> None:
        postings, skipped = find_job_postings(
            text_fixture("cn_jsonld_page.html")
        )

        self.assertEqual(len(postings), 2)
        self.assertEqual(skipped, 1)
        self.assertEqual(postings[0]["title"], "算法工程师（校招）")

    def test_single_object_and_list_documents(self) -> None:
        single = (
            '<script type="application/ld+json">'
            '{"@type": "JobPosting", "title": "A"}</script>'
        )
        listed = (
            '<script type="application/ld+json">'
            '[{"@type": "JobPosting", "title": "A"},'
            ' {"@type": "Organization", "name": "X"}]</script>'
        )

        self.assertEqual(len(find_job_postings(single)[0]), 1)
        self.assertEqual(len(find_job_postings(listed)[0]), 1)

    def test_non_jobposting_documents_are_ignored(self) -> None:
        block = (
            '<script type="application/ld+json">'
            '{"@type": "WebSite", "name": "X"}</script>'
        )

        self.assertEqual(find_job_postings(block), ([], 0))

    def test_malformed_input_never_raises(self) -> None:
        for payload in (None, 42, "", "<html><script>", "<a href="):
            with self.subTest(payload=payload):
                postings, skipped = find_job_postings(payload)
                self.assertEqual(postings, [])
                self.assertGreaterEqual(skipped, 0)

    def test_fields_mapping_keeps_only_present_values(self) -> None:
        posting = {
            "@type": "JobPosting",
            "title": "算法工程师",
            "url": "https://x/1",
            "identifier": {"value": 2001},
            "datePosted": "2026-09-21T09:00:00+08:00",
            "validThrough": "2026-10-31",
            "hiringOrganization": {"name": "某公司"},
            "jobLocation": {
                "address": {
                    "addressLocality": "杭州市",
                    "addressRegion": "浙江省",
                }
            },
        }

        fields = json_ld_to_job_fields(posting)

        self.assertEqual(fields["title"], "算法工程师")
        self.assertEqual(fields["url"], "https://x/1")
        self.assertEqual(fields["identifier"], "2001")
        self.assertEqual(fields["company"], "某公司")
        self.assertEqual(fields["location"], "杭州市")
        self.assertNotIn("salary", fields)

    def test_empty_json_ld_script_is_skipped(self) -> None:
        result = extract_json_ld(
            '<script type="application/ld+json">   </script>'
        )

        self.assertEqual(result.blocks, [])
        self.assertEqual(result.skipped, 1)


class LinkTest(unittest.TestCase):
    def test_links_are_resolved_and_unsafe_schemes_are_skipped(self) -> None:
        html = (
            '<a href="/campus/2003">测试工程师</a>'
            '<a href="job/2004">相对路径</a>'
            '<a href="javascript:void(0)">无效</a>'
            '<a href="mailto:hr@example.com">邮件</a>'
            '<a href="#top">锚点</a>'
        )

        result = extract_links(html, base_url="https://x/campus/")

        self.assertEqual(
            result.links,
            [
                ("测试工程师", "https://x/campus/2003"),
                ("相对路径", "https://x/campus/job/2004"),
            ],
        )
        self.assertEqual(result.skipped, 3)

    def test_base_tag_is_respected(self) -> None:
        html = '<base href="https://cdn.example.com/"><a href="a">A</a>'

        result = extract_links(html, base_url="https://x/")

        self.assertEqual(result.links, [("A", "https://cdn.example.com/a")])

    def test_fragment_is_dropped_from_resolved_links(self) -> None:
        self.assertEqual(
            resolve_link("/a#frag", base_url="https://x/"),
            "https://x/a",
        )
        self.assertIsNone(resolve_link("javascript:x", base_url="https://x/"))
        self.assertIsNone(resolve_link(None, base_url="https://x/"))

    def test_meta_extraction(self) -> None:
        html = (
            '<meta property="og:title" content="标题">'
            '<meta name="description" content="描述">'
            '<meta property="og:title" content="second wins nothing">'
        )

        meta = extract_meta(html)

        self.assertEqual(meta["og:title"], "标题")
        self.assertEqual(meta["description"], "描述")

    def test_malformed_markup_never_raises(self) -> None:
        self.assertEqual(extract_links("<a href=").links, [])
        self.assertEqual(extract_meta(None), {})
        self.assertEqual(extract_links(42).links, [])


if __name__ == "__main__":
    unittest.main()
