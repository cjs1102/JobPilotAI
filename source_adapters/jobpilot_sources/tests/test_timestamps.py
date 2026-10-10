"""F13 deadline/timezone tests (ExecPlan 0014, docs/03 section 9.1).

Covers Beijing <-> UTC conversion, date round-trips, day boundaries, explicit
instants, every accepted source format, ambiguous input, and the requirement
that no conversion depends on the host's timezone.
"""

from __future__ import annotations

import os
import time
import unittest
from datetime import date, datetime, timedelta, timezone
from unittest import mock

from jobpilot_sources.timestamps import (
    BEIJING_TZ,
    BEIJING_TZ_NAME,
    DB_TIMESTAMP_FORMAT,
    beijing_date_to_stored_utc,
    is_date_only_stored,
    is_valid_utc_timestamp,
    normalize_deadline,
    normalize_published_at,
    normalize_timestamp,
    parse_epoch_millis_utc,
    stored_utc_to_beijing,
    stored_utc_to_beijing_date,
)


class BeijingConversionTest(unittest.TestCase):
    def test_date_only_is_beijing_noon_stored_as_utc(self) -> None:
        self.assertEqual(BEIJING_TZ_NAME, "Asia/Shanghai")
        self.assertEqual(BEIJING_TZ.utcoffset(None), timedelta(hours=8))
        self.assertEqual(
            beijing_date_to_stored_utc(date(2026, 10, 8)),
            "2026-10-08 04:00:00",
        )

    def test_date_round_trips_without_shifting(self) -> None:
        for day in (
            date(2026, 1, 1),
            date(2026, 6, 15),
            date(2026, 10, 8),
            date(2026, 12, 31),
            date(2028, 2, 29),
            date(1000, 1, 1),
            date(9999, 12, 31),
        ):
            with self.subTest(day=day):
                stored = beijing_date_to_stored_utc(day)
                self.assertTrue(is_date_only_stored(stored))
                self.assertEqual(
                    stored_utc_to_beijing_date(stored),
                    day.isoformat(),
                )

    def test_day_boundaries(self) -> None:
        # 16:00 UTC is the next Beijing calendar day.
        self.assertEqual(
            stored_utc_to_beijing_date("2026-10-07 16:00:00"),
            "2026-10-08",
        )
        self.assertEqual(
            stored_utc_to_beijing_date("2026-10-07 15:59:59"),
            "2026-10-07",
        )
        self.assertEqual(
            stored_utc_to_beijing_date("2026-12-31 16:00:00"),
            "2027-01-01",
        )
        self.assertEqual(
            stored_utc_to_beijing_date("2028-02-29 20:00:00"),
            "2028-03-01",
        )

    def test_beijing_local_round_trip(self) -> None:
        local = stored_utc_to_beijing("2026-10-08 04:00:00")

        self.assertIsNotNone(local)
        assert local is not None
        self.assertEqual(local.strftime(DB_TIMESTAMP_FORMAT), "2026-10-08 12:00:00")
        self.assertEqual(local.tzinfo, BEIJING_TZ)

    def test_invalid_stored_values(self) -> None:
        for value in ("", "2026-10-08", "2026-10-08T04:00:00", "x"):
            with self.subTest(value=value):
                self.assertFalse(is_valid_utc_timestamp(value))
                self.assertIsNone(stored_utc_to_beijing(value))
                self.assertFalse(is_date_only_stored(value))


class ExplicitTimeTest(unittest.TestCase):
    def test_naive_datetime_is_interpreted_as_beijing(self) -> None:
        normalized = normalize_timestamp("2026-10-31 18:30:00")

        self.assertIsNotNone(normalized)
        assert normalized is not None
        self.assertEqual(normalized.utc, "2026-10-31 10:30:00")
        self.assertFalse(normalized.date_only)

    def test_real_instant_is_preserved_not_rewritten_to_noon(self) -> None:
        with_offset = normalize_timestamp("2026-10-31T18:30:00+08:00")
        utc_input = normalize_timestamp("2026-10-31T10:30:00Z")

        self.assertEqual(with_offset, utc_input)
        assert with_offset is not None
        self.assertEqual(with_offset.utc, "2026-10-31 10:30:00")
        self.assertFalse(with_offset.date_only)
        self.assertEqual(with_offset.utc[11:], "10:30:00")

    def test_datetime_object_keeps_its_instant(self) -> None:
        moment = datetime(2026, 10, 31, 10, 30, tzinfo=timezone.utc)
        normalized = normalize_timestamp(moment)

        assert normalized is not None
        self.assertEqual(normalized.utc, "2026-10-31 10:30:00")
        self.assertFalse(normalized.date_only)

    def test_iso_and_slash_and_chinese_formats(self) -> None:
        expected = "2026-10-08 04:00:00"

        for value in (
            "2026-10-08",
            "2026/10/08",
            "2026.10.08",
            "2026年10月8日",
            " 2026-10-08 ",
        ):
            with self.subTest(value=value):
                normalized = normalize_timestamp(value)
                assert normalized is not None
                self.assertEqual(normalized.utc, expected)
                self.assertTrue(normalized.date_only)

        explicit = normalize_timestamp("2026年10月8日 18:30")

        assert explicit is not None
        self.assertEqual(explicit.utc, "2026-10-08 10:30:00")
        self.assertFalse(explicit.date_only)

    def test_relative_times_need_and_use_an_explicit_now(self) -> None:
        now = datetime(2026, 10, 8, 12, 0, tzinfo=BEIJING_TZ)

        self.assertIsNone(normalize_timestamp("3天前"))
        self.assertIsNone(normalize_timestamp("刚刚"))

        cases = {
            "刚刚": "2026-10-08 04:00:00",
            "30秒前": "2026-10-08 03:59:30",
            "30分钟前": "2026-10-08 03:30:00",
            "2小时前": "2026-10-08 02:00:00",
            "3天前": "2026-10-05 04:00:00",
        }

        for value, expected in cases.items():
            with self.subTest(value=value):
                normalized = normalize_timestamp(value, now=now)
                assert normalized is not None
                self.assertEqual(normalized.utc, expected)
                self.assertFalse(normalized.date_only)

    def test_ambiguous_or_impossible_values_are_omitted(self) -> None:
        for value in (
            "10月8日",  # no year
            "2026-13-01",
            "2026-02-29",
            "2026-10-08 24:00",
            "2026-10-08 25:00",
            "下周一",
            "",
            "   ",
            "2026",
            None,
            12345,
        ):
            with self.subTest(value=value):
                self.assertIsNone(normalize_timestamp(value))

    def test_epoch_millis_helper(self) -> None:
        self.assertEqual(
            parse_epoch_millis_utc(1793442600000),
            "2026-10-31 10:30:00",
        )
        self.assertIsNone(parse_epoch_millis_utc(True))
        self.assertIsNone(parse_epoch_millis_utc("1793442600000"))

    def test_deadline_alias_and_published_at(self) -> None:
        self.assertEqual(
            normalize_deadline("2026-10-31"),
            normalize_timestamp("2026-10-31"),
        )
        self.assertEqual(
            normalize_published_at("2026-10-31"),
            "2026-10-31 04:00:00",
        )
        self.assertIsNone(normalize_published_at("下周一"))


class HostTimezoneIndependenceTest(unittest.TestCase):
    def _with_zone(self, zone: str):
        previous = os.environ.get("TZ")

        if hasattr(time, "tzset"):
            os.environ["TZ"] = zone
            time.tzset()

        return previous

    def _restore(self, previous: str | None) -> None:
        if previous is None:
            os.environ.pop("TZ", None)
        else:
            os.environ["TZ"] = previous

        if hasattr(time, "tzset"):
            time.tzset()

    def test_results_are_identical_under_any_host_timezone(self) -> None:
        if not hasattr(time, "tzset"):  # pragma: no cover - Windows
            self.skipTest("tzset is unavailable on this platform")

        expected = {
            "date": beijing_date_to_stored_utc(date(2026, 10, 8)),
            "roundtrip": stored_utc_to_beijing_date("2026-10-07 16:00:00"),
            "explicit": normalize_timestamp("2026-10-31 18:30:00"),
            "iso": normalize_timestamp("2026-10-07T16:00:00Z"),
        }

        previous = None

        try:
            for zone in (
                "UTC",
                "Asia/Shanghai",
                "America/New_York",
                "Pacific/Kiritimati",
                "Pacific/Pago_Pago",
            ):
                previous = self._with_zone(zone)

                with self.subTest(zone=zone):
                    self.assertEqual(
                        beijing_date_to_stored_utc(date(2026, 10, 8)),
                        expected["date"],
                    )
                    self.assertEqual(
                        stored_utc_to_beijing_date("2026-10-07 16:00:00"),
                        expected["roundtrip"],
                    )
                    self.assertEqual(
                        normalize_timestamp("2026-10-31 18:30:00"),
                        expected["explicit"],
                    )
                    self.assertEqual(
                        normalize_timestamp("2026-10-07T16:00:00Z"),
                        expected["iso"],
                    )
        finally:
            self._restore(previous)

    def test_conversion_never_calls_local_time_functions(self) -> None:
        # If any code path used the host clock conversion, these stubs would
        # turn the calls into failures.
        with mock.patch(
            "time.localtime", side_effect=AssertionError("localtime used")
        ), mock.patch(
            "time.mktime", side_effect=AssertionError("mktime used")
        ):
            self.assertEqual(
                beijing_date_to_stored_utc(date(2026, 10, 8)),
                "2026-10-08 04:00:00",
            )
            self.assertEqual(
                stored_utc_to_beijing_date("2026-10-07 16:00:00"),
                "2026-10-08",
            )

            normalized = normalize_timestamp("2026-10-31 18:30:00")
            assert normalized is not None
            self.assertEqual(normalized.utc, "2026-10-31 10:30:00")


if __name__ == "__main__":
    unittest.main()
