"""Batch planning and publishing tests."""

from __future__ import annotations

import json
import unittest

from jobpilot_sources.client import HttpClient, HttpError
from jobpilot_sources.publisher import (
    MAX_BATCH_SIZE,
    build_batches,
    publish_batches,
)

from .support import RecordingTransport, api_response


def make_jobs(count: int) -> list[dict[str, object]]:
    return [
        {
            "company": "Example",
            "title": f"Job {index}",
            "source": "greenhouse",
            "source_url": f"https://example.invalid/{index}",
            "requirements": [],
        }
        for index in range(count)
    ]


class BuildBatchesTest(unittest.TestCase):
    def test_single_complete_batch(self) -> None:
        batches = build_batches(
            source="greenhouse",
            scope="acme",
            observed_at="2026-10-08 12:00:00",
            jobs=make_jobs(2),
            complete=True,
        )

        self.assertEqual(len(batches), 1)
        self.assertEqual(batches[0]["source"], "greenhouse")
        self.assertEqual(batches[0]["source_scope"], "acme")
        self.assertTrue(batches[0]["complete"])
        self.assertEqual(batches[0]["observed_at"], "2026-10-08 12:00:00")
        self.assertEqual(len(batches[0]["jobs"]), 2)

    def test_exact_limit_stays_complete(self) -> None:
        batches = build_batches(
            source="lever",
            scope="acme",
            observed_at="2026-10-08 12:00:00",
            jobs=make_jobs(MAX_BATCH_SIZE),
            complete=True,
        )

        self.assertEqual(len(batches), 1)
        self.assertTrue(batches[0]["complete"])

    def test_board_above_limit_becomes_incomplete(self) -> None:
        jobs = make_jobs(217)
        batches = build_batches(
            source="greenhouse",
            scope="acme",
            observed_at="2026-10-08 12:00:00",
            jobs=jobs,
            complete=True,
        )

        self.assertEqual(len(batches), 2)
        self.assertEqual(len(batches[0]["jobs"]), MAX_BATCH_SIZE)
        self.assertEqual(len(batches[1]["jobs"]), 17)

        for batch in batches:
            self.assertFalse(batch["complete"])

    def test_incomplete_observation_is_chunked_incomplete(self) -> None:
        batches = build_batches(
            source="ashby",
            scope="acme",
            observed_at="2026-10-08 12:00:00",
            jobs=make_jobs(250),
            complete=False,
        )

        self.assertEqual([len(batch["jobs"]) for batch in batches], [200, 50])

        for batch in batches:
            self.assertFalse(batch["complete"])

    def test_empty_complete_board_is_publishable(self) -> None:
        batches = build_batches(
            source="lever",
            scope="acme",
            observed_at="2026-10-08 12:00:00",
            jobs=[],
            complete=True,
        )

        self.assertEqual(len(batches), 1)
        self.assertEqual(batches[0]["jobs"], [])
        self.assertTrue(batches[0]["complete"])

    def test_empty_incomplete_board_is_not_publishable(self) -> None:
        batches = build_batches(
            source="lever",
            scope="acme",
            observed_at="2026-10-08 12:00:00",
            jobs=[],
            complete=False,
        )

        self.assertEqual(batches, [])


class PublishBatchesTest(unittest.TestCase):
    def test_posts_each_batch_with_the_internal_token(self) -> None:
        transport = RecordingTransport(
            default=api_response(
                {"created": 1, "updated": 0, "seen": 1,
                 "closed": 0, "complete": True}
            )
        )
        client = HttpClient(transport=transport)

        batches = build_batches(
            source="greenhouse",
            scope="acme",
            observed_at="2026-10-08 12:00:00",
            jobs=make_jobs(2),
            complete=True,
        )

        responses = publish_batches(
            client,
            base_url="http://127.0.0.1:8080/",
            token="secret-token",
            batches=batches,
        )

        self.assertEqual(len(responses), 1)
        self.assertEqual(responses[0]["created"], 1)

        posts = transport.posts()
        self.assertEqual(len(posts), 1)
        self.assertEqual(
            posts[0]["url"],
            "http://127.0.0.1:8080/internal/jobs/source-sync",
        )
        self.assertEqual(
            posts[0]["headers"]["X-Internal-Token"],
            "secret-token",
        )
        self.assertNotIn("secret-token", posts[0]["url"])

        body = json.loads(posts[0]["body"].decode("utf-8"))
        self.assertNotIn("secret-token", json.dumps(body))
        self.assertEqual(body["source_scope"], "acme")
        self.assertEqual(len(body["jobs"]), 2)

    def test_publish_propagates_http_errors(self) -> None:
        transport = RecordingTransport(
            default=(401, b'{"code":20001}')
        )
        client = HttpClient(transport=transport)

        with self.assertRaises(HttpError):
            publish_batches(
                client,
                base_url="http://127.0.0.1:8080",
                token="secret-token",
                batches=[
                    {
                        "source": "lever",
                        "source_scope": "acme",
                        "complete": True,
                        "observed_at": "2026-10-08 12:00:00",
                        "jobs": [],
                    }
                ],
            )


if __name__ == "__main__":
    unittest.main()
