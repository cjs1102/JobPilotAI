"""Nowcoder provider tests (fixture driven; no network).

Covers the two record shapes, the recursive ``jobListData`` lookup, the
bounded 20-row window, fail-closed parsing, row-level diagnostics, the 48 KiB
description cap and in-batch dedupe.
"""

from __future__ import annotations

import json
import unittest

from jobpilot_sources.providers import nowcoder
from jobpilot_sources.robots import parse_robots
from jobpilot_sources.tests.support import text_fixture


def page(records, module="999"):
    state = {"app": {module: {"jobListData": records}}, "store": {}}
    return (
        "<!doctype html><html><body>"
        f"<script>window.__INITIAL_STATE__={json.dumps(state, ensure_ascii=False)}"
        "</script></body></html>"
    )


def native_row(**overrides):
    row = {
        "id": 404338,
        "jobName": "嵌入式软件工程师",
        "companyNameText": "Formlabs",
        "jobCity": "深圳",
        "jobCityList": ["深圳"],
        "salaryText": "25-35K·12薪",
        "eduLevel": 5000,
        "recruitType": 1,
        "createTime": 1754033527000,
        "deliverEnd": 1798732799000,
        "ext": json.dumps(
            {"requirements": "熟悉C++。", "infos": "负责嵌入式开发。"},
            ensure_ascii=False,
        ),
    }
    row.update(overrides)
    return row


def aggregator_row(**overrides):
    row = {
        "id": 30300000146411,
        "jobTitle": "后端开发工程师",
        "jobName": "后端开发工程师",
        "companyName": "重庆千里科技股份有限公司",
        "companyNameText": "重庆千里科技股份有限公司",
        "city": None,
        "jobCity": "",
        "jobCityList": [],
        "salary": "薪资面议",
        "salaryText": "薪资面议",
        "education": "本科",
        "recruitType": 0,
        "createTime": 1787378565000,
        "platform": "moka",
        "description": "<p>职位描述</p><p>负责后端系统开发。</p>",
    }
    row.update(overrides)
    return row


class BoardUrlTest(unittest.TestCase):
    def test_keyword_only(self) -> None:
        url = nowcoder.board_url("后端")
        self.assertTrue(url.startswith(nowcoder.BASE_URL))
        self.assertIn("search=%E5%90%8E%E7%AB%AF", url)
        self.assertNotIn("city=", url)

    def test_keyword_and_city(self) -> None:
        url = nowcoder.board_url("C++|深圳")
        self.assertIn("search=C%2B%2B", url)
        self.assertIn("city=%E6%B7%B1%E5%9C%B3", url)

    def test_invalid_boards_are_rejected(self) -> None:
        for board in ("", "|深圳", "后端|", "a|b|c", "后端\n"):
            with self.subTest(board=board):
                with self.assertRaises(Exception):
                    nowcoder.board_url(board)

    def test_interval_is_conservative(self) -> None:
        self.assertGreaterEqual(nowcoder.MIN_INTERVAL_SECONDS, 5.0)


class NativeFixtureTest(unittest.TestCase):
    def setUp(self) -> None:
        self.board = nowcoder.parse_board(
            text_fixture("nowcoder_search_native.html"),
            board="C++|深圳",
        )

    def test_result_is_always_incomplete(self) -> None:
        self.assertEqual(self.board.source, "nowcoder")
        self.assertFalse(self.board.complete)
        self.assertEqual(self.board.total, 2)
        self.assertEqual(len(self.board.jobs), 2)
        self.assertEqual(self.board.diagnostics, [])

    def test_native_mapping(self) -> None:
        job = self.board.jobs[0]

        self.assertEqual(job["source"], "nowcoder")
        self.assertEqual(job["title"], "嵌入式软件工程师")
        self.assertEqual(job["company"], "Formlabs")
        self.assertEqual(job["location"], "深圳")
        self.assertEqual(job["salary_min"], 25)
        self.assertEqual(job["salary_max"], 35)
        self.assertEqual(job["salary_currency"], "CNY")
        self.assertEqual(job["external_id"], "404338")
        self.assertEqual(
            job["source_url"],
            "https://www.nowcoder.com/jobs/detail/404338",
        )
        self.assertEqual(job["published_at"], "2025-08-01 07:32:07")
        self.assertEqual(job["deadline"], "2026-12-31 15:59:59")
        self.assertIs(job["deadline_date_only"], False)
        self.assertEqual(job["job_nature"], 1)
        self.assertNotIn("education_required", job)
        self.assertIn("进行3D打印机嵌入式系统软件开发", job["description"])
        self.assertIn("熟练掌握C/C++编程", job["description"])
        self.assertEqual(job["requirements"], [])

    def test_second_native_row_uses_config_fallback_only_when_needed(self) -> None:
        job = self.board.jobs[1]
        self.assertEqual(job["company"], "思谋科技（SmartMore）")
        self.assertEqual(job["title"], "【27届】应用开发工程师（C++）")

    def test_recursive_lookup_does_not_depend_on_132(self) -> None:
        self.assertEqual(len(self.board.jobs), 2)


class AggregatorFixtureTest(unittest.TestCase):
    def setUp(self) -> None:
        self.board = nowcoder.parse_board(
            text_fixture("nowcoder_search_aggregator.html"),
            board="后端",
        )

    def test_mixed_shapes_are_both_parsed(self) -> None:
        self.assertEqual(len(self.board.jobs), 2)
        self.assertFalse(self.board.complete)
        self.assertEqual(self.board.diagnostics, [])

    def test_aggregator_mapping(self) -> None:
        job = self.board.jobs[1]

        self.assertEqual(job["title"], "后端开发工程师")
        self.assertEqual(job["company"], "重庆千里科技股份有限公司")
        self.assertNotIn("location", job)
        self.assertNotIn("salary_min", job)
        self.assertNotIn("salary_max", job)
        self.assertEqual(job["education_required"], 3)
        self.assertNotIn("job_nature", job)
        self.assertEqual(job["external_id"], "30300000146411")
        self.assertEqual(
            job["source_url"],
            "https://www.nowcoder.com/jobs/detail/30300000146411",
        )
        self.assertEqual(job["published_at"], "2026-08-22 06:02:45")
        self.assertIn("职位描述", job["description"])
        self.assertIn("负责无人驾驶网约车平台调度", job["description"])
        self.assertNotIn("<p>", job["description"])


class FailClosedTest(unittest.TestCase):
    def test_empty_job_list_is_a_valid_empty_result(self) -> None:
        board = nowcoder.parse_board(
            text_fixture("nowcoder_empty.html"),
            board="后端",
        )

        self.assertEqual(board.jobs, [])
        self.assertEqual(board.total, 0)
        self.assertFalse(board.complete)

    def test_missing_initial_state_is_an_error(self) -> None:
        with self.assertRaisesRegex(Exception, "INITIAL_STATE"):
            nowcoder.parse_board(
                text_fixture("nowcoder_missing_state.html"),
                board="后端",
            )

    def test_missing_job_list_data_is_an_error(self) -> None:
        payload = (
            "<script>window.__INITIAL_STATE__="
            '{"app":{"999":{"other":[]}}}'
            "</script>"
        )

        with self.assertRaisesRegex(Exception, "jobListData"):
            nowcoder.parse_board(payload, board="后端")

    def test_invalid_state_json_is_an_error(self) -> None:
        payload = "<script>window.__INITIAL_STATE__={not json</script>"

        with self.assertRaisesRegex(Exception, "valid JSON"):
            nowcoder.parse_board(payload, board="后端")

    def test_non_text_payload_is_an_error(self) -> None:
        with self.assertRaises(Exception):
            nowcoder.parse_board({"jobListData": []}, board="后端")


class RowDiagnosticsTest(unittest.TestCase):
    def parse(self, rows):
        return nowcoder.parse_board(page(rows), board="后端")

    def test_missing_fields_skip_the_row_and_keep_the_rest(self) -> None:
        rows = [
            native_row(id=None),
            native_row(jobName=None),
            native_row(companyNameText=None),
            native_row(id=400001),
        ]
        board = self.parse(rows)

        self.assertEqual(len(board.jobs), 1)
        reasons = [item["reason"] for item in board.diagnostics]
        self.assertEqual(
            reasons,
            ["missing_external_id", "missing_title", "missing_company"],
        )

    def test_invalid_ext_json_skips_the_row(self) -> None:
        board = self.parse([native_row(ext="{bad json"), native_row(id=400002)])

        self.assertEqual(len(board.jobs), 1)
        self.assertEqual(board.diagnostics[0]["reason"], "invalid_ext_json")

    def test_unrecognised_shape_skips_the_row(self) -> None:
        board = self.parse([{"id": 123456, "foo": "bar"}])

        self.assertEqual(board.jobs, [])
        self.assertEqual(
            board.diagnostics[0]["reason"], "unrecognised_record_shape"
        )

    def test_description_over_48_kib_is_rejected_not_truncated(self) -> None:
        huge = "<p>" + ("x" * (48 * 1024 + 10)) + "</p>"
        board = self.parse([aggregator_row(description=huge)])

        self.assertEqual(board.jobs, [])
        diagnostic = board.diagnostics[0]
        self.assertEqual(diagnostic["reason"], "description_too_large")
        self.assertEqual(diagnostic["limit_bytes"], 48 * 1024)
        self.assertGreater(diagnostic["actual_bytes"], 48 * 1024)
        self.assertNotIn("x" * 100, json.dumps(diagnostic, ensure_ascii=False))

    def test_duplicate_content_in_batch_keeps_the_first(self) -> None:
        board = self.parse(
            [
                native_row(id=400001),
                native_row(id=400002),
            ]
        )

        self.assertEqual(len(board.jobs), 1)
        self.assertEqual(board.jobs[0]["external_id"], "400001")
        self.assertEqual(board.diagnostics[0]["reason"], "duplicate_in_batch")
        self.assertEqual(board.diagnostics[0]["external_id"], "400002")

    def test_twenty_rows_are_accepted(self) -> None:
        rows = [
            native_row(id=500000 + index, jobName=f"C++ 开发 {index}")
            for index in range(20)
        ]
        board = self.parse(rows)

        self.assertEqual(len(board.jobs), 20)
        self.assertFalse(board.complete)


class RobotsFixtureTest(unittest.TestCase):
    def test_recorded_robots_allows_the_job_paths(self) -> None:
        rules = parse_robots(
            text_fixture("nowcoder_robots_2026-10-19.txt"),
            user_agent=(
                "jobpilot-source-adapters/0.1 "
                "(+https://github.com/jobpilotai; public job board reader)"
            ),
        )

        self.assertEqual(rules.verdict("/jobs/school/jobs"), "allowed")
        self.assertEqual(rules.verdict("/jobs/detail/404338"), "allowed")
        self.assertEqual(rules.verdict("/search"), "disallowed")
        self.assertEqual(rules.verdict("/nccommon/x"), "disallowed")
        self.assertEqual(rules.verdict("/ab/ab-test-flow/x"), "disallowed")


if __name__ == "__main__":
    unittest.main()
