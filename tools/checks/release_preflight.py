#!/usr/bin/env python3
"""Release preflight: the automatically checkable items of docs/RELEASE_CHECKLIST.md.

Offline and stdlib-only. Every check prints PASS, FAIL or SKIP plus its raw
evidence lines; a SKIP is never reported as green. The exit code is 0 only when
no check FAILed. The manual items (Actions minutes, repository About/description,
visibility, PAT rotation, the last CI run) are printed as MANUAL lines with the
page to look at, because nothing on this host can read them (the repository is
private and this host holds no GitHub token).

Usage::

    python3 tools/checks/release_preflight.py [--json] [--allow-dirty]

`--allow-dirty` downgrades the working-tree-clean requirement to INFO; use it
while a release is still being prepared, never for the release commit itself.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

PASS, FAIL, SKIP, INFO = "PASS", "FAIL", "SKIP", "INFO"

MAX_TRACKED_FILE_BYTES = 5 * 1024 * 1024

# (name, pattern) — concrete secret shapes only. Generic `password = ...`
# matches are deliberately not scanned: this repository documents ephemeral CI
# credentials and local development placeholders, and a noisy check is a check
# nobody trusts (docs/quality/README.md records the same reasoning).
SECRET_PATTERNS = (
    ("github classic token", re.compile(r"\bghp_[A-Za-z0-9]{20,}\b")),
    ("github fine-grained token", re.compile(r"\bgithub_pat_[A-Za-z0-9_]{20,}\b")),
    ("private key block", re.compile(r"-----BEGIN [A-Z ]*PRIVATE KEY-----")),
    ("aws access key id", re.compile(r"\bAKIA[0-9A-Z]{16}\b")),
    ("slack token", re.compile(r"\bxox[baprs]-[A-Za-z0-9-]{10,}\b")),
    ("openai-style key", re.compile(r"\bsk-[A-Za-z0-9]{20,}\b")),
)

PRIVATE_IPV4 = re.compile(
    r"\b(?:10\.\d{1,3}|192\.168|172\.(?:1[6-9]|2\d|3[01]))\.\d{1,3}\.\d{1,3}\b"
)

# A developer-machine path is not a secret, but it has no business in a public
# release: the tree keeps its evidence reproducible without naming anybody's
# home directory (the 0017 overlap audit was caught by this).
LOCAL_ABSOLUTE_PATH = re.compile(
    r"(?:/home/[A-Za-z0-9._-]+/|/Users/[A-Za-z0-9._-]+/|[A-Za-z]:\\\\Users\\\\[A-Za-z0-9._-]+\\\\)"
)

# A private address inside a test source is a fixture (the SSRF/guard tests need
# one), not leaked infrastructure. Anything else is a finding.
TEST_PATH = re.compile(r"(^|/)tests?/|(^|/)test_[^/]*$")

NOTICE_ALIASES = {
    "spdlog": ("spdlog",),
    "yaml-cpp": ("yaml-cpp",),
    "nlohmann_json": ("nlohmann",),
    "gtest": ("googletest", "gtest"),
    "hiredis": ("hiredis",),
}

MANUAL_ITEMS = (
    ("actions-minutes", "Settings -> Billing -> Actions minutes used this month "
                        "(private repositories are metered): https://github.com/settings/billing"),
    ("last-ci-run", "the newest `ci` and `image` workflow runs on main must be green: "
                    "https://github.com/cjs1102/JobPilotAI/actions"),
    ("repository-about", "About/description/topics are filled in on the repository home page "
                         "(owner action; not readable from here)"),
    ("repository-visibility", "the repository is public (owner action; this host sees 404 "
                              "for the anonymous API, which is exactly what a private repo does)"),
    ("pat-rotation", "the previously used personal access token is revoked/rotated (owner action)"),
)


def run_git(root: Path, *args: str) -> tuple[int, str]:
    completed = subprocess.run(
        ["git", *args], cwd=root, capture_output=True, text=True, check=False
    )
    return completed.returncode, (completed.stdout or completed.stderr).strip()


def tracked_files(root: Path) -> list[str]:
    code, out = run_git(root, "ls-files", "-z")
    if code != 0:
        return []
    return [name for name in out.split("\0") if name]


def read_text(path: Path) -> str | None:
    try:
        return path.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError):
        return None


def check_license(root: Path) -> tuple[str, list[str]]:
    evidence: list[str] = []
    license_text = read_text(root / "LICENSE")
    if license_text is None:
        return FAIL, ["LICENSE is missing or not readable UTF-8"]
    lines = [line for line in license_text.splitlines() if line.strip()]
    evidence.append(f"LICENSE: {len(lines)} non-empty lines")
    if "MIT" not in license_text:
        return FAIL, evidence + ["LICENSE does not name the MIT licence"]
    evidence.append("LICENSE names the MIT licence")
    notices = read_text(root / "THIRD_PARTY_NOTICES.md")
    if notices is None:
        return FAIL, evidence + ["THIRD_PARTY_NOTICES.md is missing or not readable UTF-8"]
    evidence.append(f"THIRD_PARTY_NOTICES.md: {len(notices.splitlines())} lines")
    return PASS, evidence


def check_notices(root: Path) -> tuple[str, list[str]]:
    conanfile = read_text(root / "service/conanfile.txt")
    notices = read_text(root / "THIRD_PARTY_NOTICES.md")
    if conanfile is None or notices is None:
        return FAIL, ["service/conanfile.txt or THIRD_PARTY_NOTICES.md is unreadable"]

    requirements: list[str] = []
    in_requires = False
    for line in conanfile.splitlines():
        stripped = line.strip()
        if stripped.startswith("["):
            in_requires = stripped == "[requires]"
            continue
        if in_requires and stripped and not stripped.startswith("#"):
            requirements.append(stripped.split("/")[0])
    if not requirements:
        return FAIL, ["service/conanfile.txt declares no [requires] entries"]

    evidence = [f"conanfile requires: {', '.join(requirements)}"]
    missing = []
    folded = notices.casefold()
    for requirement in requirements:
        aliases = NOTICE_ALIASES.get(requirement, (requirement,))
        if any(alias.casefold() in folded for alias in aliases):
            evidence.append(f"  covered: {requirement} (matched {'/'.join(aliases)})")
        else:
            missing.append(requirement)
    if missing:
        return FAIL, evidence + [f"not mentioned in THIRD_PARTY_NOTICES.md: {', '.join(missing)}"]
    if "libmysqlclient" not in notices:
        return FAIL, evidence + ["the non-Conan MySQL client dependency is not mentioned"]
    evidence.append("  covered: libmysqlclient (non-Conan, mentioned)")
    return PASS, evidence


def check_secrets(root: Path) -> tuple[str, list[str]]:
    evidence: list[str] = []
    findings: list[str] = []
    allowed: list[str] = []
    scanned = 0
    for name in tracked_files(root):
        path = root / name
        text = read_text(path)
        if text is None:
            continue
        scanned += 1
        is_test = bool(TEST_PATH.search(name))
        for line_number, line in enumerate(text.splitlines(), start=1):
            for label, pattern in SECRET_PATTERNS:
                if pattern.search(line):
                    findings.append(f"{name}:{line_number}: {label}")
            if PRIVATE_IPV4.search(line):
                entry = f"{name}:{line_number}: private IPv4 address"
                (allowed if is_test else findings).append(entry)
    evidence.append(f"scanned {scanned} tracked text files")
    if allowed:
        evidence.append(f"allowed as test fixtures ({len(allowed)}): {allowed[0]}"
                        + (f" (+{len(allowed) - 1} more)" if len(allowed) > 1 else ""))
    if (root / ".env").exists() and ".env" in tracked_files(root):
        findings.append(".env is tracked by Git")
    if findings:
        return FAIL, evidence + [f"  {entry}" for entry in findings]
    evidence.append("no secret shape and no non-test private address found")
    return PASS, evidence


def check_local_paths(root: Path) -> tuple[str, list[str]]:
    """Absolute developer-machine paths must not reach a public release."""
    findings: list[str] = []
    scanned = 0
    for name in tracked_files(root):
        text = read_text(root / name)
        if text is None:
            continue
        scanned += 1
        for line_number, line in enumerate(text.splitlines(), start=1):
            match = LOCAL_ABSOLUTE_PATH.search(line)
            if match:
                findings.append(f"{name}:{line_number}: {match.group(0)}<...>")
    evidence = [f"scanned {scanned} tracked text files"]
    if findings:
        return FAIL, evidence + [f"  {entry}" for entry in findings]
    evidence.append("no absolute developer-machine path found")
    return PASS, evidence


def check_readme(root: Path) -> tuple[str, list[str]]:
    readme = read_text(root / "README.md")
    if readme is None:
        return FAIL, ["README.md is missing or not readable UTF-8"]
    head = "\n".join(readme.splitlines()[:60])
    requirements = (
        ("## Quick Start (English)", "an English Quick Start section in the first 60 lines"),
        ("tools/dev/bootstrap.sh", "the documented dependency bootstrap command"),
        ("cmake --preset", "the documented configure command"),
        ("docker compose", "the documented local run command"),
        ("docs/README.md", "a pointer into the documentation index"),
    )
    evidence: list[str] = []
    missing = []
    for needle, description in requirements:
        if needle in head or needle in readme:
            evidence.append(f"  present: {description}")
        else:
            missing.append(description)
    if missing:
        return FAIL, evidence + ["missing from README.md: " + "; ".join(missing)]
    return PASS, evidence


def check_links(root: Path) -> tuple[str, list[str]]:
    targets = ["README.md", "THIRD_PARTY_NOTICES.md", "docs/README.md",
               "docs/RELEASE_CHECKLIST.md"]
    link_pattern = re.compile(r"\[[^\]]*\]\(([^)\s]+)\)")
    evidence: list[str] = []
    missing: list[str] = []
    checked = 0
    for target in targets:
        text = read_text(root / target)
        if text is None:
            evidence.append(f"  {target}: not present")
            continue
        base = (root / target).parent
        for match in link_pattern.finditer(text):
            raw = match.group(1)
            if raw.startswith(("http://", "https://", "mailto:", "#")):
                continue
            relative = raw.split("#", 1)[0]
            if not relative:
                continue
            checked += 1
            if not (base / relative).exists():
                missing.append(f"{target} -> {raw}")
    evidence.append(f"checked {checked} relative links in {', '.join(targets)}")
    if missing:
        return FAIL, evidence + [f"  missing: {entry}" for entry in missing]
    return PASS, evidence


def check_workflows(root: Path) -> tuple[str, list[str]]:
    workflow_dir = root / ".github/workflows"
    workflows = sorted(workflow_dir.glob("*.yml")) + sorted(workflow_dir.glob("*.yaml"))
    if not workflows:
        return FAIL, ["no workflow file found under .github/workflows/"]
    evidence = [f"workflow files: {', '.join(path.name for path in workflows)}"]
    for path in workflows:
        text = read_text(path) or ""
        if not text.startswith("name:") and "\non:" not in text and not text.startswith("on:"):
            evidence.append(f"  {path.name}: does not look like a workflow (no name/on)")
    lint = subprocess.run(
        ["bash", "-lc", "command -v actionlint"], capture_output=True, text=True, check=False
    )
    actionlint = lint.stdout.strip() or os.environ.get("ACTIONLINT", "").strip()
    if not actionlint:
        return SKIP, evidence + [
            "actionlint is not on PATH (and $ACTIONLINT is unset); install it (e.g. "
            "`go install github.com/rhysd/actionlint/cmd/actionlint@latest`) and re-run"
        ]
    completed = subprocess.run(
        [actionlint, *[str(path) for path in workflows]],
        cwd=root, capture_output=True, text=True, check=False,
    )
    output = (completed.stdout + completed.stderr).strip()
    if completed.returncode != 0:
        return FAIL, evidence + [f"actionlint rc={completed.returncode}"] + output.splitlines()
    # Print the version, never the local binary path: evidence must stay portable
    # and must not leak a developer-machine path into the repository.
    version = subprocess.run(
        [actionlint, "--version"], capture_output=True, text=True, check=False
    ).stdout.strip().splitlines()
    label = version[0] if version else "actionlint"
    evidence.append(f"actionlint rc=0 ({label})")
    return PASS, evidence


def check_hygiene(root: Path, allow_dirty: bool) -> tuple[str, list[str]]:
    evidence: list[str] = []
    names = tracked_files(root)
    if not names:
        return FAIL, ["git ls-files returned nothing; run this from the repository"]

    if ".env" in names:
        return FAIL, evidence + [".env is tracked"]
    if any(name.startswith("build/") for name in names):
        return FAIL, evidence + ["files under build/ are tracked"]
    largest = max(names, key=lambda name: (root / name).stat().st_size
                  if (root / name).exists() else 0)
    largest_size = (root / largest).stat().st_size if (root / largest).exists() else 0
    evidence.append(f"largest tracked file: {largest} ({largest_size} bytes)")
    if largest_size > MAX_TRACKED_FILE_BYTES:
        return FAIL, evidence + [f"a tracked file exceeds {MAX_TRACKED_FILE_BYTES} bytes"]

    code, status = run_git(root, "status", "--porcelain")
    if code != 0:
        return FAIL, evidence + ["git status failed"]
    if status:
        first = status.splitlines()[0]
        if allow_dirty:
            evidence.append(f"working tree is dirty (allowed by --allow-dirty): {first}")
        else:
            return FAIL, evidence + [
                f"working tree is dirty: {first} (+{max(len(status.splitlines()) - 1, 0)} more)"
            ]
    else:
        evidence.append("working tree is clean")
    return PASS, evidence


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--json", action="store_true", help="machine-readable output")
    parser.add_argument("--allow-dirty", action="store_true",
                        help="treat a dirty working tree as INFO instead of FAIL")
    arguments = parser.parse_args(argv)

    code, toplevel = run_git(Path.cwd(), "rev-parse", "--show-toplevel")
    if code != 0 or not toplevel:
        print("release preflight: not inside a Git working tree", file=sys.stderr)
        return 2
    root = Path(toplevel)
    _, head = run_git(root, "rev-parse", "--short", "HEAD")

    checks = [
        ("license", lambda: check_license(root)),
        ("notices", lambda: check_notices(root)),
        ("secrets", lambda: check_secrets(root)),
        ("local-paths", lambda: check_local_paths(root)),
        ("readme", lambda: check_readme(root)),
        ("links", lambda: check_links(root)),
        ("workflows", lambda: check_workflows(root)),
        ("hygiene", lambda: check_hygiene(root, arguments.allow_dirty)),
    ]

    results = []
    for name, function in checks:
        status, evidence = function()
        results.append({"name": name, "status": status, "evidence": evidence})

    failures = [row for row in results if row["status"] == FAIL]

    if arguments.json:
        print(json.dumps(
            {
                "root": str(root),
                "head": head,
                "results": results,
                "manual": [{"name": name, "instruction": text} for name, text in MANUAL_ITEMS],
                "failures": len(failures),
            },
            ensure_ascii=False,
            indent=2,
        ))
    else:
        print(f"release preflight (offline) - {root} @ {head or 'unknown'}")
        for row in results:
            print(f"{row['status']} {row['name']}: {row['evidence'][0] if row['evidence'] else ''}")
            for line in row["evidence"][1:]:
                print(f"      {line}")
        print()
        for name, instruction in MANUAL_ITEMS:
            print(f"MANUAL {name}: {instruction}")
        passed = sum(1 for row in results if row["status"] == PASS)
        skipped = sum(1 for row in results if row["status"] == SKIP)
        print()
        print(f"summary: {passed} pass, {len(failures)} fail, {skipped} skip, "
              f"{len(MANUAL_ITEMS)} manual")
        if failures:
            print("RELEASE_PREFLIGHT_FAILED")

    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
