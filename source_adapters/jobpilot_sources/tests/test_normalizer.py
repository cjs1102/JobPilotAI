"""Normalization helper tests."""

from __future__ import annotations

import unittest

from jobpilot_sources.normalizer import (
    as_unsigned_int,
    build_job,
    clean_identifier,
    clean_multiline,
    clean_text,
    first_present,
    html_to_text,
    parse_epoch_millis_utc,
    parse_iso8601_utc,
)


class CleanTextTest(unittest.TestCase):
    def test_trims_and_collapses(self) -> None:
        self.assertEqual(clean_text("  Backend   Engineer \n"), "Backend Engineer")

    def test_rejects_empty_and_non_strings(self) -> None:
        self.assertIsNone(clean_text("   "))
        self.assertIsNone(clean_text(None))
        self.assertIsNone(clean_text(42))

    def test_identifier_keeps_integers(self) -> None:
        self.assertEqual(clean_identifier(8860302002), "8860302002")
        self.assertEqual(clean_identifier(" abc "), "abc")
        self.assertIsNone(clean_identifier(True))
        self.assertIsNone(clean_identifier(None))

    def test_multiline_keeps_paragraph_breaks(self) -> None:
        self.assertEqual(
            clean_multiline("  first line  \n\n second line \r\n"),
            "first line\n\n second line",
        )

    def test_multiline_rejects_empty(self) -> None:
        self.assertIsNone(clean_multiline("   \n  "))
        self.assertIsNone(clean_multiline(None))


class HtmlToTextTest(unittest.TestCase):
    def test_strips_markup_and_unescapes(self) -> None:
        self.assertEqual(
            html_to_text(
                "<p>Build <strong>backend</strong> services.</p>"
                "<ul><li>C++ &amp; Linux</li></ul>"
            ),
            "Build backend services.\nC++ & Linux",
        )

    def test_rejects_empty(self) -> None:
        self.assertIsNone(html_to_text(""))
        self.assertIsNone(html_to_text(None))


class TimestampTest(unittest.TestCase):
    def test_iso8601_with_offset_becomes_utc(self) -> None:
        self.assertEqual(
            parse_iso8601_utc("2026-10-02T11:31:50-04:00"),
            "2026-10-02 15:31:50",
        )

    def test_iso8601_with_microseconds_and_z(self) -> None:
        self.assertEqual(
            parse_iso8601_utc("2026-04-07T17:12:35.753+00:00"),
            "2026-04-07 17:12:35",
        )
        self.assertEqual(
            parse_iso8601_utc("2026-04-07T17:12:35Z"),
            "2026-04-07 17:12:35",
        )

    def test_naive_timestamp_is_treated_as_utc(self) -> None:
        self.assertEqual(
            parse_iso8601_utc("2026-04-07T17:12:35"),
            "2026-04-07 17:12:35",
        )

    def test_invalid_iso8601_returns_none(self) -> None:
        self.assertIsNone(parse_iso8601_utc("not-a-timestamp"))
        self.assertIsNone(parse_iso8601_utc(""))
        self.assertIsNone(parse_iso8601_utc(None))

    def test_epoch_millis_becomes_utc(self) -> None:
        self.assertEqual(
            parse_epoch_millis_utc(1788199939703),
            "2026-08-31 18:12:19",
        )

    def test_invalid_epoch_returns_none(self) -> None:
        self.assertIsNone(parse_epoch_millis_utc("1788199939703"))
        self.assertIsNone(parse_epoch_millis_utc(True))
        self.assertIsNone(parse_epoch_millis_utc(None))


class SmallHelperTest(unittest.TestCase):
    def test_first_present_skips_empty(self) -> None:
        self.assertEqual(first_present(None, "", "  ", "value"), "value")
        self.assertIsNone(first_present(None, ""))

    def test_as_unsigned_int(self) -> None:
        self.assertEqual(as_unsigned_int(85000), 85000)
        self.assertEqual(as_unsigned_int(12.0), 12)
        self.assertIsNone(as_unsigned_int(-1))
        self.assertIsNone(as_unsigned_int(1.5))
        self.assertIsNone(as_unsigned_int("12"))
        self.assertIsNone(as_unsigned_int(True))


class BuildJobTest(unittest.TestCase):
    def test_requires_contract_fields(self) -> None:
        job = build_job(
            source="greenhouse",
            source_url="https://example.invalid/1",
            title="Backend",
            company="Example",
        )

        self.assertEqual(job["company"], "Example")
        self.assertEqual(job["title"], "Backend")
        self.assertEqual(job["source"], "greenhouse")
        self.assertEqual(job["source_url"], "https://example.invalid/1")
        self.assertEqual(job["requirements"], [])

    def test_omits_unknown_optional_fields(self) -> None:
        job = build_job(
            source="lever",
            source_url="https://example.invalid/2",
            title="Backend",
            company="Example",
        )

        for key in (
            "external_id",
            "published_at",
            "location",
            "description",
            "salary_min",
            "salary_max",
            "salary_currency",
        ):
            self.assertNotIn(key, job)

    def test_keeps_known_optionals_and_identifiers(self) -> None:
        job = build_job(
            source="ashby",
            source_url="https://example.invalid/3",
            title="Backend",
            company="Example",
            external_id=34413,
            published_at="2026-04-07 17:12:35",
            location="Remote",
            description="Text",
            salary_currency="CNY",
            salary_min=20,
            salary_max=35,
        )

        self.assertEqual(job["external_id"], "34413")
        self.assertEqual(job["published_at"], "2026-04-07 17:12:35")
        self.assertEqual(job["location"], "Remote")
        self.assertEqual(job["description"], "Text")
        self.assertEqual(job["salary_currency"], "CNY")
        self.assertEqual(job["salary_min"], 20)
        self.assertEqual(job["salary_max"], 35)

    def test_negative_salary_is_dropped_not_fabricated(self) -> None:
        job = build_job(
            source="lever",
            source_url="https://example.invalid/4",
            title="Backend",
            company="Example",
            salary_min=-5,
        )

        self.assertNotIn("salary_min", job)


if __name__ == "__main__":
    unittest.main()
