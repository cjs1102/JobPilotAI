#!/usr/bin/env python3
"""0015 phase C (decision 2 = C1): render a read-only, self-contained HTML report.

The report renders EXACTLY what the public API returned -- it never re-scores,
re-sorts or enriches a row. Two input modes:

    # offline (no network, no session): render a captured response body
    python3 tools/job_report/render_report.py \
        --input captured_jobs.json --output job_report.html

    # one read-only GET with an existing Bearer token (no new endpoint/auth)
    python3 tools/job_report/render_report.py \
        --url 'http://127.0.0.1:8080/api/v1/jobs?with_match=true&page_size=50' \
        --token "$JOBPILOT_TOKEN" --output job_report.html

Contract (docs/04 section 8):
  * read-only: the only request is a GET of the jobs list (or none at all in
    --input mode); no POST/PUT/DELETE, no database access, no state change;
  * no new dependency: Python standard library only;
  * self-contained: one HTML file, inline CSS, no external asset, no script;
  * the ORIGINAL posting link is present in every row;
  * deterministic ordering: rows keep the API's order, rank = position;
  * a row without reasons gets an explicit marker instead of an empty cell.
"""

import argparse
import html
import json
import sys
import urllib.error
import urllib.request
from datetime import datetime, timezone

# Rows without reasons must be explicit, never blank (plan section 5.3).
NO_REASONS_MARKER = "无匹配理由"
NO_DEAL_BREAKERS_MARKER = "无"
MISSING_MARKER = "—"

STYLE = """
body { font-family: system-ui, sans-serif; margin: 24px; color: #1f2328; }
h1 { font-size: 20px; margin: 0 0 4px 0; }
.meta { color: #57606a; font-size: 13px; margin-bottom: 16px; }
table { border-collapse: collapse; width: 100%; font-size: 13px; }
th, td { border: 1px solid #d0d7de; padding: 6px 8px; vertical-align: top;
         text-align: left; }
th { background: #f6f8fa; }
tr:nth-child(even) td { background: #fcfcfd; }
.score { font-weight: 600; }
.version { color: #57606a; font-size: 11px; }
.tag { display: inline-block; border: 1px solid #d0d7de; border-radius: 10px;
       padding: 0 6px; margin: 0 4px 2px 0; font-size: 11px; }
.tag.breaker { border-color: #d1242f; color: #d1242f; }
.tag.urgency-expired { border-color: #d1242f; color: #d1242f; }
.tag.urgency-urgent { border-color: #bf8700; color: #bf8700; }
.none { color: #57606a; }
a { color: #0969da; }
"""


def escape(value):
    if value is None:
        return MISSING_MARKER
    return html.escape(str(value))


def cell(value):
    """Text cell: None/empty becomes an explicit dash marker."""
    if value is None or value == "":
        return '<span class="none">%s</span>' % MISSING_MARKER
    return escape(value)


def tag_list(values, css_class=""):
    if not values:
        return '<span class="none">%s</span>' % MISSING_MARKER
    class_attr = ' class="tag %s"' % css_class if css_class else ' class="tag"'
    parts = []
    for value in values:
        parts.append(
            "<span%s>%s</span>" % (class_attr, escape(value))
        )
    return "".join(parts)


def render_reasons(reasons):
    if not reasons:
        return '<span class="none">%s</span>' % NO_REASONS_MARKER
    parts = []
    for reason in reasons:
        code = reason.get("code", "")
        text = reason.get("text", "")
        evidence = reason.get("evidence", "")
        parts.append(
            "<div><span class=\"tag\">%s</span>%s"
            ' <span class="version">[%s]</span></div>'
            % (escape(code), escape(text), escape(evidence))
        )
    return "".join(parts)


def render_breakers(breakers):
    if not breakers:
        return '<span class="none">%s</span>' % NO_DEAL_BREAKERS_MARKER
    parts = []
    for breaker in breakers:
        parts.append(
            '<div><span class="tag breaker">%s</span>%s</div>'
            % (escape(breaker.get("code", "")),
               escape(breaker.get("text", "")))
        )
    return "".join(parts)


def urgency_class(urgency):
    if urgency in ("expired", "urgent"):
        return "tag urgency-%s" % urgency
    return "tag"


def render_rows(items):
    rows = []
    for index, item in enumerate(items):
        urgency = item.get("deadline_urgency") or "unknown"
        score = item.get("score")
        score_text = (
            "%.2f" % score if isinstance(score, (int, float))
            else MISSING_MARKER
        )
        source_url = item.get("source_url")
        if source_url:
            link = '<a href="%s" rel="noopener noreferrer">原始岗位</a>' % (
                escape(source_url)
            )
        else:
            link = '<span class="none">%s</span>' % MISSING_MARKER

        score_cell = (
            '<span class="score">%s</span><br>'
            '<span class="version">%s</span>'
            % (score_text, escape(item.get("score_version", MISSING_MARKER)))
        )
        urgency_cell = '<span class="%s">%s</span>' % (
            urgency_class(urgency), escape(urgency)
        )

        cells = [
            str(index + 1),
            score_cell,
            cell(item.get("company")),
            cell(item.get("title")),
            cell(item.get("location")),
            cell(item.get("freshness")),
            tag_list(item.get("matched_skills") or []),
            tag_list(item.get("missing_skills") or []),
            render_reasons(item.get("reasons") or []),
            render_breakers(item.get("deal_breakers") or []),
            urgency_cell,
            link,
        ]

        rows.append(
            "<tr>%s</tr>"
            % "".join("<td>%s</td>" % value for value in cells)
        )

    return "\n".join(rows)


def render_report(payload, generated_at, title="岗位匹配报告",
                  sort_basis="API 返回顺序（未重排）"):
    """Render the captured list response into one self-contained HTML file.

    `payload` is the parsed API response body: the envelope {"data": {...}}
    (or a bare {"list": [...]} for convenience).
    """
    data = payload.get("data", payload)
    items = data.get("list") or []
    total = data.get("total")
    page = data.get("page")
    page_size = data.get("page_size")
    truncated = data.get("truncated")

    meta_bits = ["行数 %d" % len(items)]
    if total is not None:
        meta_bits.append("total %s" % escape(total))
    if page is not None and page_size is not None:
        meta_bits.append("page %s / %s" % (escape(page), escape(page_size)))
    if truncated is not None:
        meta_bits.append("truncated %s" % escape(truncated))

    if not items:
        body = '<p class="none">响应中没有岗位行。</p>'
    else:
        body = (
            "<table>"
            "<thead><tr>"
            "<th>#</th><th>score</th><th>公司</th><th>职位</th><th>地点</th>"
            "<th>freshness</th><th>匹配技能</th><th>缺失技能</th><th>理由</th>"
            "<th>deal-breaker</th><th>截止紧迫度</th><th>原始链接</th>"
            "</tr></thead><tbody>%s</tbody></table>"
        ) % render_rows(items)

    return (
        "<!DOCTYPE html>\n"
        '<html lang="zh-CN"><head><meta charset="utf-8">'
        "<title>%s</title><style>%s</style></head><body>"
        "<h1>%s</h1>"
        '<div class="meta">生成时间 %s · %s · 排序基准 %s · 数据直接来自 '
        "<code>GET /api/v1/jobs?with_match=true</code>，未重新打分也未重排。</div>"
        "%s"
        "</body></html>\n"
    ) % (
        escape(title),
        STYLE,
        escape(title),
        escape(generated_at),
        " · ".join(meta_bits),
        escape(sort_basis),
        body,
    )


def load_input(args):
    if args.input:
        with open(args.input, "r", encoding="utf-8") as handle:
            return json.load(handle)

    request = urllib.request.Request(args.url, method="GET")
    if args.token:
        request.add_header("Authorization", "Bearer %s" % args.token)
    with urllib.request.urlopen(request, timeout=args.timeout) as response:
        return json.loads(response.read().decode("utf-8"))


def build_parser():
    parser = argparse.ArgumentParser(
        description="Render a read-only, self-contained job match report."
    )
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--input", help="captured API response body (JSON file)")
    source.add_argument("--url", help="jobs list URL (a single read-only GET)")
    parser.add_argument("--token", help="Bearer token for --url mode")
    parser.add_argument("--timeout", type=float, default=15.0)
    parser.add_argument("--output", default="job_report.html")
    parser.add_argument("--title", default="岗位匹配报告")
    parser.add_argument(
        "--sort-basis",
        default="API 返回顺序（未重排）",
        help="text recorded as the ordering basis (never applied by the tool)",
    )
    parser.add_argument(
        "--generated-at",
        help="explicit timestamp (deterministic output for tests)",
    )
    return parser


def main(argv=None):
    args = build_parser().parse_args(argv)
    generated_at = args.generated_at or datetime.now(timezone.utc).strftime(
        "%Y-%m-%d %H:%M:%S UTC"
    )

    try:
        payload = load_input(args)
    except (OSError, ValueError, urllib.error.URLError) as error:
        print("failed to read input: %s" % error, file=sys.stderr)
        return 1

    document = render_report(payload, generated_at, args.title,
                             args.sort_basis)

    with open(args.output, "w", encoding="utf-8") as handle:
        handle.write(document)

    print("wrote %s (%d rows)" % (
        args.output, len((payload.get("data", payload).get("list") or []))
    ))
    return 0


if __name__ == "__main__":
    sys.exit(main())
