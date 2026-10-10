#!/usr/bin/env python3
"""Opt-in end-to-end verification of the manual-capture channel (ExecPlan 0014 Step 6).

This script is **not** part of the unittest suite: it needs a running
``jobpilot_service`` (MySQL + Redis) and writes one job row. It proves, against
the real service:

1. ``publish-capture`` imports a bookmarklet capture through the existing
   ``POST /internal/jobs/import`` endpoint with ``source="manual"``;
2. the job is visible through the public query interface
   (``GET /api/v1/jobs`` and ``GET /api/v1/jobs/{id}``) with the 0014 fields;
3. **F14**: a ``POST /internal/jobs/source-sync`` observation whose
   ``source_url`` collides with the manual row does **not** change it
   (``skipped_manual`` is reported and the public detail is byte-identical
   before/after).

Usage::

    MYSQL_PASSWORD=... INTERNAL_API_TOKEN=... \\
      ./build/presets/debug/service/jobpilot_service --config=/tmp/e2e.yaml &

    E2E_TOKEN=... python3 source_adapters/tools/verify_manual_capture_e2e.py \\
      --base-url http://127.0.0.1:18080 --token-env E2E_TOKEN

The unique ``source_url`` it creates is printed so it can be deleted afterwards:

    DELETE FROM job_requirements WHERE job_id = <id>;
    DELETE FROM jobs WHERE id = <id>;
"""

from __future__ import annotations

import argparse
import json
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Any

SOURCE_ADAPTERS = Path(__file__).resolve().parents[1]

if str(SOURCE_ADAPTERS) not in sys.path:
    sys.path.insert(0, str(SOURCE_ADAPTERS))

from jobpilot_sources.capture import (  # noqa: E402
    ManualPublisher,
    build_import_payloads,
    parse_captures,
    resolve_publish_token,
)
from jobpilot_sources.client import UrllibTransport  # noqa: E402
from jobpilot_sources.fetch import normalize_url  # noqa: E402

PASS = "PASS"
FAIL = "FAIL"


class VerificationError(Exception):
    pass


def _request(
    method: str,
    url: str,
    *,
    body: dict[str, Any] | None = None,
    headers: dict[str, str] | None = None,
    timeout: float = 10.0,
) -> tuple[int, Any]:
    payload = json.dumps(body).encode("utf-8") if body is not None else None
    request = urllib.request.Request(
        url,
        data=payload,
        headers=headers or {},
        method=method,
    )

    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            raw = response.read()
            status = response.status
    except urllib.error.HTTPError as error:
        raw = error.read()
        status = error.code

    try:
        return status, json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError):
        return status, raw.decode("utf-8", "replace")


def check(label: str, condition: bool, detail: str = "") -> None:
    print(f"[{PASS if condition else FAIL}] {label}" + (f" — {detail}" if detail else ""))

    if not condition:
        raise VerificationError(label)


def build_capture(source_url: str, token: str, unique: str) -> list[Any]:
    document = {
        "capture_version": 1,
        "captured_at": "2026-10-09T12:00:00.000Z",
        "page_url": source_url,
        "page_title": "E2E 验证岗位",
        "job": {
            # The keyword is carried by the title because the public search only
            # matches title/company/description, not source_url.
            "title": f"E2E 手动采集后端工程师 {unique}",
            "company": "E2E 示例公司",
            "source_url": source_url,
            "location": "深圳市-南山区",
            "salary": "15-25K·14薪",
            "education": "本科及以上",
            "job_nature": "校招",
            "experience": "应届生",
            "deadline": "截止时间：2026-10-31",
            "published_at": "2026-09-20 08:00:00",
            "description": "E2E manual capture description",
            "skills": ["C++"],
        },
        "warnings": [],
    }

    assert token not in json.dumps(document)

    return parse_captures(document)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base-url", required=True)
    parser.add_argument("--token-env", default="INTERNAL_API_TOKEN")
    parser.add_argument("--source", default="greenhouse")
    parser.add_argument(
        "--keyword", default=None, help="search keyword (default: unique token)"
    )
    arguments = parser.parse_args(argv)

    base_url = arguments.base_url.rstrip("/")
    token = resolve_publish_token(arguments.token_env)

    unique = f"e2e-{int(time.time())}"
    source_url = f"https://e2e.invalid/manual/{unique}{token[-4:]}"
    keyword = arguments.keyword or unique

    print(f"base_url={base_url}")
    print(f"source_url={source_url}")

    captures = build_capture(source_url, token, unique)
    payloads, notes = build_import_payloads(captures)

    outcome = ManualPublisher(
        base_url=base_url,
        token=token,
        transport=UrllibTransport(),
    ).publish(payloads, notes=notes)

    check("publish imported exactly one manual job", outcome.created == 1, str(outcome))

    status, search = _request(
        "GET", f"{base_url}/api/v1/jobs?keyword={urllib.parse.quote(keyword)}"
    )
    check("public search responds 200", status == 200, str(status))

    entries = search.get("data", {}).get("list", []) if isinstance(search, dict) else []
    matching = [
        entry
        for entry in entries
        if entry.get("title") == f"E2E 手动采集后端工程师 {unique}"
    ]
    check("manual job is visible in public search", len(matching) == 1)
    summary = matching[0]

    check("source stays manual", summary.get("source") == "manual")
    check("education_required round-trips", summary.get("education_required") == 3)
    check("job_nature round-trips", summary.get("job_nature") == 1)
    check(
        "date-only deadline renders as a Beijing date",
        summary.get("deadline") == "2026-10-31",
    )
    check("deadline_date_only round-trips", summary.get("deadline_date_only") is True)
    check("manual import leaves freshness unobserved", summary.get("last_seen_at") is None)
    check("manual import leaves freshness null", summary.get("freshness") is None)

    job_id = summary.get("id")
    check("summary carries an id", isinstance(job_id, int) and job_id > 0, str(job_id))

    status, before = _request("GET", f"{base_url}/api/v1/jobs/{job_id}")
    check("detail responds 200", status == 200, str(status))
    before_data = before.get("data", {})

    observation = {
        "source": arguments.source,
        "source_scope": f"{unique}-scope",
        "complete": False,
        "observed_at": "2026-10-09 12:00:00",
        "jobs": [
            {
                "company": "E2E 示例公司",
                "title": "E2E 观测到的标题（不得覆盖人工记录）",
                "source": arguments.source,
                "source_url": source_url,
                "description": "observed description must not win",
                "education_required": 1,
                "job_nature": 3,
                "deadline": "2026-11-15 10:30:00",
                "deadline_date_only": False,
            }
        ],
    }

    status, sync = _request(
        "POST",
        f"{base_url}/internal/jobs/source-sync",
        body=observation,
        headers={
            "Content-Type": "application/json",
            "X-Internal-Token": token,
        },
    )
    check("source-sync responds 200", status == 200, json.dumps(sync, ensure_ascii=False))
    check("observation skipped the manual record", sync.get("skipped_manual") == 1, str(sync))
    check(
        "skip is diagnosable by url",
        sync.get("skipped_manual_urls") == [source_url],
        str(sync.get("skipped_manual_urls")),
    )

    status, after = _request("GET", f"{base_url}/api/v1/jobs/{job_id}")
    check("detail still responds 200 after the sync", status == 200, str(status))
    after_data = after.get("data", {})

    check(
        "F14: the manual record is byte-identical before and after the sync",
        before_data == after_data,
        "changed fields: "
        + ", ".join(
            key
            for key in sorted(set(before_data) | set(after_data))
            if before_data.get(key) != after_data.get(key)
        ),
    )
    check("F14: source is still manual", after_data.get("source") == "manual")
    check(
        "F14: human title and description survived",
        after_data.get("title") == f"E2E 手动采集后端工程师 {unique}"
        and after_data.get("description") == "E2E manual capture description",
    )

    print(f"\ncleanup: DELETE FROM job_requirements WHERE job_id = {job_id};")
    print(f"cleanup: DELETE FROM jobs WHERE id = {job_id};")
    print(f"normalized source_url: {normalize_url(source_url)}")
    print("\nALL CHECKS PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
