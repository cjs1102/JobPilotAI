"""Deterministic Chinese field mapping tests (enums, years, location, deadline)."""

from __future__ import annotations

import unittest
from datetime import date

from jobpilot_sources import normalize_cn
from jobpilot_sources.timestamps import beijing_date_to_stored_utc


class EducationTest(unittest.TestCase):
    def test_known_levels(self) -> None:
        cases = {
            "不限": 1,
            "学历不限": 1,
            "大专": 2,
            "专科及以上": 2,
            "本科": 3,
            "本科及以上": 3,
            "硕士": 4,
            "研究生": 4,
            "博士研究生": 5,
        }

        for text, expected in cases.items():
            with self.subTest(text=text):
                self.assertEqual(normalize_cn.normalize_education(text), expected)

    def test_unknown_values_are_omitted(self) -> None:
        for text in ("MBA", "中专", "other", "", None, 3):
            with self.subTest(text=text):
                self.assertIsNone(normalize_cn.normalize_education(text))


class JobNatureTest(unittest.TestCase):
    def test_known_natures(self) -> None:
        cases = {
            "校招": 1,
            "校园招聘": 1,
            "应届生": 1,
            "社招": 2,
            "社会招聘": 2,
            "实习": 3,
            "实习生": 3,
            # Internship wording wins when both appear.
            "校招实习": 3,
        }

        for text, expected in cases.items():
            with self.subTest(text=text):
                self.assertEqual(normalize_cn.normalize_job_nature(text), expected)

    def test_unknown_values_are_omitted(self) -> None:
        for text in ("internship", "internal audit", "unknown", "", None):
            with self.subTest(text=text):
                self.assertIsNone(normalize_cn.normalize_job_nature(text))


class ExperienceYearsTest(unittest.TestCase):
    def test_ranges_and_bounds(self) -> None:
        cases = {
            "3-5年": (3, 5),
            "1-3年经验": (1, 3),
            "3年以上": (3, None),
            "1年及以上": (1, None),
            "5年以下": (None, 5),
            "3年": (3, 3),
            "应届生": (0, None),
            "无经验": (0, None),
            "经验不限": (None, None),
        }

        for text, expected in cases.items():
            with self.subTest(text=text):
                result = normalize_cn.normalize_experience_years(text)

                self.assertIsNotNone(result, text)
                assert result is not None
                self.assertEqual((result.minimum, result.maximum), expected)

    def test_unknown_and_invalid_values_are_omitted(self):
        for text in ("资深", "", None, 3):
            with self.subTest(text=text):
                self.assertIsNone(normalize_cn.normalize_experience_years(text))

    def test_inverted_range_is_rejected(self) -> None:
        self.assertIsNone(normalize_cn.normalize_experience_years("5-3年"))


class LocationTest(unittest.TestCase):
    def test_city_and_district_combine(self) -> None:
        self.assertEqual(
            normalize_cn.normalize_location("深圳市", "南山区"),
            "深圳-南山区",
        )
        self.assertEqual(normalize_cn.normalize_location("北京市"), "北京")
        self.assertEqual(
            normalize_cn.normalize_location(None, "南山区"), "南山区"
        )
        self.assertIsNone(normalize_cn.normalize_location(None, None))

    def test_combined_text_normalisation_is_conservative(self) -> None:
        self.assertEqual(
            normalize_cn.normalize_location_text("深圳 · 南山区"),
            "深圳-南山区",
        )
        self.assertEqual(normalize_cn.normalize_location_text("深圳市"), "深圳")
        self.assertEqual(
            normalize_cn.normalize_location_text("深圳市南山区"),
            "深圳市南山区",
        )
        self.assertIsNone(normalize_cn.normalize_location_text("  "))


class DeadlineMappingTest(unittest.TestCase):
    def test_date_only_deadlines_use_the_beijing_noon_convention(self) -> None:
        cases = {
            "截止时间：2026-10-31": "2026-10-31",
            "报名截止 2026年10月31日": "2026-10-31",
            "2026/10/31": "2026-10-31",
            "投递截止：2026-10-31（含）": "2026-10-31",
        }

        for text, expected_day in cases.items():
            with self.subTest(text=text):
                result = normalize_cn.normalize_deadline_fields(text)

                self.assertIsNotNone(result, text)
                assert result is not None
                self.assertTrue(result.date_only)
                self.assertEqual(
                    result.utc,
                    beijing_date_to_stored_utc(
                        date.fromisoformat(expected_day)
                    ),
                )

    def test_explicit_deadline_keeps_its_instant(self) -> None:
        result = normalize_cn.normalize_deadline_fields(
            "截止时间：2026-10-31 18:30:00"
        )

        self.assertIsNotNone(result)
        assert result is not None
        self.assertFalse(result.date_only)
        self.assertEqual(result.utc, "2026-10-31 10:30:00")

    def test_ambiguous_deadline_is_omitted(self) -> None:
        for text in ("长期有效", "招满为止", "10月31日", "", None):
            with self.subTest(text=text):
                self.assertIsNone(normalize_cn.normalize_deadline_fields(text))

    def test_label_stripping(self) -> None:
        self.assertEqual(
            normalize_cn.strip_deadline_labels("截止时间：2026-10-31"),
            "2026-10-31",
        )
        self.assertEqual(
            normalize_cn.strip_deadline_labels("2026-10-31 之前"),
            "2026-10-31",
        )
        self.assertEqual(normalize_cn.strip_deadline_labels(None), None)


if __name__ == "__main__":
    unittest.main()
