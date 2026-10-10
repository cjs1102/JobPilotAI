"""Salary normalisation tests: representable forms only, never a conversion."""

from __future__ import annotations

import unittest

from jobpilot_sources.salary import (
    parse_monthly_salary_thousand,
    parse_salary_currency,
)


class SalaryTest(unittest.TestCase):
    def test_representable_monthly_forms(self) -> None:
        cases = {
            "15-25K": (15, 25),
            "15k-25k": (15, 25),
            "8k-12k": (8, 12),
            "15K-25K·14薪": (15, 25),
            "1.5-2.5万/月": (15, 25),
            "1.5万/月": (15, 15),
            "月薪1.2万": (12, 12),
            "8000-12000元/月": (8, 12),
            "6-8千/月": (6, 8),
            "15K以上": (15, None),
            "20K以下": (None, 20),
            "20-35K，14薪": (20, 35),
        }

        for text, expected in cases.items():
            with self.subTest(text=text):
                self.assertEqual(
                    parse_monthly_salary_thousand(text), expected
                )

    def test_ambiguous_or_non_monthly_forms_are_omitted(self) -> None:
        for text in (
            "面议",
            "薪资面议",
            "300/天",
            "300元/天",
            "30-50万/年",
            "年薪30万",
            "8000",
            "8.5K",
            "1.2345万/月",
            "5-3K",
            "$20-30K",
            "20-30K USD",
            "月薪保密",
            "",
            None,
            15000,
        ):
            with self.subTest(text=text):
                self.assertIsNone(parse_monthly_salary_thousand(text))

    def test_currency_is_only_reported_when_a_salary_is_published(self) -> None:
        self.assertEqual(parse_salary_currency("15-25K"), "CNY")
        self.assertIsNone(parse_salary_currency("面议"))
        self.assertIsNone(parse_salary_currency("$20-30K"))


if __name__ == "__main__":
    unittest.main()
