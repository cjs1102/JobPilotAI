#!/usr/bin/env python3
"""0015 phase D (D5) guard: prove that NO submission / auto-apply path exists.

The stage's hard boundary is that a tailored artifact is only ever *associated*
with a job/application and confirmed by a human; 0015 adds no capability to send
anything to an employer. This check makes that boundary executable:

    python3 tools/checks/no_submission_path.py            # scan the repo
    python3 tools/checks/no_submission_path.py --root DIR # scan DIR instead

It scans the tracked-style source trees (service/, backend/, ai_worker/,
source_adapters/, tools/, database/ and the docs that pin the contract) for
identifiers that would represent an auto-apply/auto-submit capability, plus
route paths whose verbs imply submission.

Exit code 0 = no submission path found; 1 = findings (file:line + token);
2 = usage error. Stdlib only.
"""

import argparse
import os
import sys

# Identifiers that only exist if something can send/apply on the user's behalf.
FORBIDDEN_TOKENS = (
    "auto_apply",
    "autoapply",
    "auto-apply",
    "auto_submit",
    "autosubmit",
    "auto-submit",
    "submit_application",
    "submitapplication",
    "send_application",
    "sendapplication",
    "one_click_apply",
    "oneclickapply",
    "apply_on_behalf",
    "autoapplyjob",
)

# Route verbs that would expose a submission endpoint.
FORBIDDEN_ROUTE_FRAGMENTS = (
    '"/submit',
    '"/apply',
    '"/send',
    "/submit\"",
    "/apply\"",
    "/send\"",
)

SCANNED_SUFFIXES = (
    ".c",
    ".cc",
    ".cpp",
    ".h",
    ".hpp",
    ".py",
    ".js",
    ".mjs",
    ".sql",
    ".yaml",
    ".yml",
)

# Documentation legitimately *discusses* the prohibition ("no auto-apply"), so
# prose is out of scope: only code, config and route definitions are scanned.
SKIPPED_DIRECTORIES = (
    ".git",
    "build",
    "docs",
    "node_modules",
    "third_party",
    "__pycache__",
    ".venv",
    "venv",
)


def iter_files(root):
    for current, directories, files in os.walk(root):
        directories[:] = [
            name for name in directories
            if name not in SKIPPED_DIRECTORIES
        ]

        for name in files:
            if name.endswith(SCANNED_SUFFIXES):
                yield os.path.join(current, name)


def scan_file(path):
    """Return [(line_number, token, line)] findings for one file."""
    findings = []

    # This checker (and its test) names the tokens by necessity.
    basename = os.path.basename(path)

    if basename in ("no_submission_path.py", "test_no_submission_path.py"):
        return findings

    # Route verbs only mean something in the route table.
    check_routes = basename in ("router.cpp", "router.h")

    try:
        with open(path, "r", encoding="utf-8", errors="replace") as handle:
            for number, line in enumerate(handle, start=1):
                lowered = line.lower()

                for token in FORBIDDEN_TOKENS:
                    if token in lowered:
                        findings.append((number, token, line.rstrip()))

                if not check_routes:
                    continue

                for fragment in FORBIDDEN_ROUTE_FRAGMENTS:
                    if fragment in line:
                        findings.append((number, fragment, line.rstrip()))
    except OSError:
        return []

    return findings


def scan(root):
    """Return [(path, line_number, token, line)] over the whole root."""
    results = []

    for path in sorted(iter_files(root)):
        for number, token, line in scan_file(path):
            results.append((path, number, token, line))

    return results


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Prove that no submission/auto-apply path exists."
    )
    parser.add_argument("--root", default=None, help="tree to scan")
    args = parser.parse_args(argv)

    root = args.root or os.path.dirname(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    )

    if not os.path.isdir(root):
        print("not a directory: %s" % root, file=sys.stderr)
        return 2

    findings = scan(root)

    if not findings:
        print(
            "no submission/auto-apply path found in %s "
            "(%d forbidden tokens, %d route fragments checked per file)"
            % (root, len(FORBIDDEN_TOKENS), len(FORBIDDEN_ROUTE_FRAGMENTS))
        )
        return 0

    for path, number, token, line in findings:
        print("%s:%d: %s | %s" % (path, number, token, line))

    print(
        "FAIL: %d submission/auto-apply finding(s)" % len(findings),
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
