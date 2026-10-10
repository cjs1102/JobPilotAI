#!/usr/bin/env python3
"""Coverage-not-degraded checker for the 0017-B test overlap audit.

Offline and stdlib-only. It reads the machine-readable merge map
(``docs/quality/test-merge-map.json``) and the live CTest list, then exits
non-zero when any of these holds:

* a ``removed`` case is still present in the live CTest list;
* a ``survivor`` case is absent from the live CTest list;
* a recorded ``binary`` does not match the survivor's live binary (the hard
  merge rule requires the removed case and the survivor to share one binary);
* a removal has no explicit proof block (same_binary / same_input /
  assertion_superset all true) or exceeds the map's ``cap``;
* the map's counts disagree with the live CTest total or with the counts block
  embedded in the report (``<!-- audit-counts: {...} -->``);
* the JSON structure is invalid.

Usage::

    python3 tools/checks/test_coverage_map.py \
        --test-dir build/presets/release \
        --map docs/quality/test-merge-map.json

``--report`` defaults to ``test-overlap-audit.md`` next to the map. Every check
prints an ``OK``/``FAIL``/``SKIP`` line with its raw evidence; ``SKIP`` is never
reported as green. The exit code is 0 only when no check FAILed.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

OK, FAIL, SKIP = "OK", "FAIL", "SKIP"

REQUIRED_TOP = ("schema_version", "counts", "removed", "survivors")
REQUIRED_COUNTS = ("before", "after", "removed", "survivors", "candidates_evaluated", "cap")
REQUIRED_REMOVAL = ("removed", "survivor", "binary", "reason", "evidence", "proof")
REQUIRED_PROOF = ("same_binary", "same_input", "assertion_superset")
REQUIRED_EVALUATED = ("removed", "survivor", "binary", "decision", "rule_failure", "reason", "evidence")
DECISIONS = ("KEEP", "RECORD-ONLY", "MERGE")

COUNTS_MARKER = re.compile(r"<!--\s*audit-counts:\s*(\{.*?\})\s*-->", re.S)
CTEST_LINE = re.compile(r"^\s*Test\s+#\d+:\s*(.+?)\s*$")

_failures: list[str] = []
_lines: list[str] = []


def emit(status: str, message: str) -> None:
    _lines.append(f"{status:<4} {message}")
    if status == FAIL:
        _failures.append(message)


def load_map(path: Path) -> dict:
    try:
        raw = path.read_text(encoding="utf-8")
    except OSError as error:
        emit(FAIL, f"map: cannot read {path}: {error}")
        return {}
    try:
        data = json.loads(raw)
    except json.JSONDecodeError as error:
        emit(FAIL, f"map: invalid JSON in {path}: {error}")
        return {}
    if not isinstance(data, dict):
        emit(FAIL, f"map: top level must be a JSON object, got {type(data).__name__}")
        return {}
    return data


def validate_structure(data: dict) -> bool:
    ok = True
    for key in REQUIRED_TOP:
        if key not in data:
            emit(FAIL, f"structure: missing top-level key {key!r}")
            ok = False
    if not isinstance(data.get("schema_version"), int):
        emit(FAIL, "structure: schema_version must be an integer")
        ok = False
    counts = data.get("counts")
    if not isinstance(counts, dict):
        emit(FAIL, "structure: counts must be an object")
        return False
    for key in REQUIRED_COUNTS:
        if not isinstance(counts.get(key), int):
            emit(FAIL, f"structure: counts.{key} must be an integer")
            ok = False
    for key in ("removed", "survivors"):
        if not isinstance(data.get(key), list):
            emit(FAIL, f"structure: {key} must be an array")
            ok = False
    if not ok:
        return False

    for index, entry in enumerate(data["removed"]):
        where = f"removed[{index}]"
        if not isinstance(entry, dict):
            emit(FAIL, f"structure: {where} must be an object")
            ok = False
            continue
        for key in REQUIRED_REMOVAL:
            if key not in entry:
                emit(FAIL, f"structure: {where} missing key {key!r}")
                ok = False
        for key in ("removed", "survivor", "binary", "reason", "evidence"):
            value = entry.get(key)
            if not isinstance(value, str) or not value.strip():
                emit(FAIL, f"structure: {where}.{key} must be a non-empty string")
                ok = False
        proof = entry.get("proof")
        if not isinstance(proof, dict):
            emit(FAIL, f"structure: {where}.proof must be an object")
            ok = False
        else:
            for key in REQUIRED_PROOF:
                if proof.get(key) is not True:
                    emit(FAIL, f"structure: {where}.proof.{key} must be true")
                    ok = False

    for index, entry in enumerate(data["survivors"]):
        if not isinstance(entry, str) or not entry.strip():
            emit(FAIL, f"structure: survivors[{index}] must be a non-empty string")
            ok = False

    evaluated = data.get("evaluated", [])
    if not isinstance(evaluated, list):
        emit(FAIL, "structure: evaluated must be an array when present")
        return False
    for index, entry in enumerate(evaluated):
        where = f"evaluated[{index}]"
        if not isinstance(entry, dict):
            emit(FAIL, f"structure: {where} must be an object")
            ok = False
            continue
        for key in REQUIRED_EVALUATED:
            if key not in entry:
                emit(FAIL, f"structure: {where} missing key {key!r}")
                ok = False
        for key in ("removed", "survivor", "binary", "rule_failure", "reason", "evidence"):
            value = entry.get(key)
            if not isinstance(value, str) or not value.strip():
                emit(FAIL, f"structure: {where}.{key} must be a non-empty string")
                ok = False
        if entry.get("decision") not in DECISIONS:
            emit(FAIL, f"structure: {where}.decision must be one of {DECISIONS}")
            ok = False
    return ok


def live_tests(test_dir: Path) -> tuple[dict[str, str], dict[str, str]]:
    """Return (name -> binary, name -> name) plus the raw ctest -N output."""
    command = ["ctest", "--test-dir", str(test_dir), "-N"]
    try:
        result = subprocess.run(command, capture_output=True, text=True, timeout=180)
    except (OSError, subprocess.SubprocessError) as error:
        emit(FAIL, f"live: cannot run {' '.join(command)}: {error}")
        return {}, {}
    names: list[str] = []
    for line in result.stdout.splitlines():
        match = CTEST_LINE.match(line)
        if match:
            names.append(match.group(1))
    if not names:
        emit(FAIL, f"live: no 'Test #N:' lines in {' '.join(command)} output (exit {result.returncode})")
        return {}, {}
    emit(OK, f"live: {len(names)} cases in {test_dir} (ctest -N, exit {result.returncode})")

    binaries: dict[str, str] = {}
    json_command = ["ctest", "--test-dir", str(test_dir), "--show-only=json-v1"]
    try:
        json_result = subprocess.run(json_command, capture_output=True, text=True, timeout=180)
        payload = json.loads(json_result.stdout)
        for test in payload.get("tests", []):
            command_line = test.get("command") or []
            if command_line:
                binaries[test["name"]] = Path(command_line[0]).name
        emit(OK, f"live: resolved {len(binaries)} case -> binary mappings (--show-only=json-v1)")
    except (OSError, subprocess.SubprocessError, json.JSONDecodeError, KeyError) as error:
        emit(SKIP, f"live: binary mapping unavailable ({error}); same-binary clause not checked")
    return {name: binaries.get(name, "") for name in names}, {name: name for name in names}


def check_removals(data: dict, live: dict[str, str]) -> None:
    removed = data["removed"]
    survivors = data["survivors"]
    cap = data["counts"]["cap"]
    if len(removed) > cap:
        emit(FAIL, f"rule: {len(removed)} removals exceed the cap of {cap}")

    names = [entry["removed"] for entry in removed]
    duplicates = sorted({name for name in names if names.count(name) > 1})
    if duplicates:
        emit(FAIL, f"rule: duplicate removed names: {', '.join(duplicates)}")
    if sorted(survivors) != sorted({entry["survivor"] for entry in removed}):
        emit(FAIL, "rule: survivors array does not match the survivor set of removed entries")

    for entry in removed:
        name, survivor = entry["removed"], entry["survivor"]
        if name in live:
            emit(FAIL, f"rule: removed case {name!r} is still present in the live CTest list")
        if survivor not in live:
            emit(FAIL, f"rule: survivor {survivor!r} is absent from the live CTest list")
        elif live[survivor] and entry["binary"] != live[survivor]:
            emit(FAIL, f"rule: {name!r} -> {survivor!r} records binary {entry['binary']!r} "
                       f"but {survivor!r} lives in {live[survivor]!r}")
        else:
            emit(OK, f"merged {name} -> {survivor} ({entry['binary']})")
    if not removed:
        emit(OK, "rule: 0 removals, every live case is kept as-is")


def check_counts(data: dict, live: dict[str, str]) -> None:
    counts = data["counts"]
    removed = len(data["removed"])
    survivors = len(data["survivors"])
    evaluated = len(data.get("evaluated", []))
    if counts["removed"] != removed:
        emit(FAIL, f"counts: counts.removed={counts['removed']} but the removed array has {removed}")
    if counts["survivors"] != survivors:
        emit(FAIL, f"counts: counts.survivors={counts['survivors']} but the survivors array has {survivors}")
    if counts["candidates_evaluated"] != evaluated:
        emit(FAIL, f"counts: counts.candidates_evaluated={counts['candidates_evaluated']} "
                   f"but the evaluated array has {evaluated}")
    if counts["before"] - counts["removed"] != counts["after"]:
        emit(FAIL, f"counts: before({counts['before']}) - removed({counts['removed']}) "
                   f"!= after({counts['after']})")
    if live and counts["after"] != len(live):
        emit(FAIL, f"counts: counts.after={counts['after']} but the live CTest list has {len(live)} cases")
    emit(OK, f"counts: before={counts['before']} removed={counts['removed']} after={counts['after']} "
             f"live={len(live)} evaluated={evaluated} cap={counts['cap']}")


def check_report(counts: dict, report: Path) -> None:
    try:
        text = report.read_text(encoding="utf-8")
    except OSError as error:
        emit(FAIL, f"report: cannot read {report}: {error}")
        return
    match = COUNTS_MARKER.search(text)
    if not match:
        emit(FAIL, f"report: no '<!-- audit-counts: {{...}} -->' block in {report}")
        return
    try:
        reported = json.loads(match.group(1))
    except json.JSONDecodeError as error:
        emit(FAIL, f"report: audit-counts block is not valid JSON: {error}")
        return
    mismatched = []
    for key in ("before", "after", "removed", "survivors", "candidates_evaluated", "record_only", "cap"):
        if key in reported and reported[key] != counts.get(key):
            mismatched.append(f"{key}: report={reported[key]} map={counts.get(key)}")
    if mismatched:
        emit(FAIL, f"report: counts disagree with the map ({'; '.join(mismatched)})")
        return
    emit(OK, f"report: {report} counts block agrees with the map "
             f"(before={reported.get('before')} after={reported.get('after')} removed={reported.get('removed')})")


def check_evaluated(data: dict, live: dict[str, str]) -> None:
    evaluated = data.get("evaluated", [])
    removed_names = {entry["removed"] for entry in data["removed"]}
    problems = 0
    for entry in evaluated:
        name, survivor = entry["removed"], entry["survivor"]
        if entry["decision"] == "MERGE" and name not in removed_names:
            emit(FAIL, f"evaluated: {name!r} is marked MERGE but is not in the removed array")
            problems += 1
        for label in (name, survivor):
            if label not in live:
                emit(FAIL, f"evaluated: kept case {label!r} is absent from the live CTest list")
                problems += 1
        if live.get(name) and live.get(survivor) and live[name] != live[survivor]:
            emit(FAIL, f"evaluated: {name!r} and {survivor!r} are not in the same binary")
            problems += 1
        if live.get(survivor) and entry["binary"] != live[survivor]:
            emit(FAIL, f"evaluated: {name!r} -> {survivor!r} records binary {entry['binary']!r} "
                       f"but the live binary is {live[survivor]!r}")
            problems += 1
    if evaluated and not problems:
        emit(OK, f"evaluated: {len(evaluated)} kept candidate pairs still present and intra-binary")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--test-dir", required=True, help="CTest build directory, e.g. build/presets/release")
    parser.add_argument("--map", required=True, help="path to docs/quality/test-merge-map.json")
    parser.add_argument("--report", default=None,
                        help="path to the audit report (default: test-overlap-audit.md next to the map)")
    parser.add_argument("--json", action="store_true", help="also print a machine-readable summary")
    args = parser.parse_args()

    map_path = Path(args.map)
    report_path = Path(args.report) if args.report else map_path.with_name("test-overlap-audit.md")

    data = load_map(map_path)
    if not data:
        print("\n".join(_lines + [f"FAIL test_coverage_map: {map_path} could not be loaded"]))
        return 1
    emit(OK, f"map: {map_path} (schema_version {data.get('schema_version')})")
    if not validate_structure(data):
        print("\n".join(_lines + ["FAIL test_coverage_map: invalid map structure"]))
        return 1

    live, _ = live_tests(Path(args.test_dir))
    check_counts(data, live)
    check_removals(data, live)
    check_report(data["counts"], report_path)
    check_evaluated(data, live)

    counts = data["counts"]
    summary = (f"test_coverage_map: {'FAIL' if _failures else 'PASS'} "
               f"({counts['before']} -> {counts['after']}, removed={counts['removed']}, "
               f"survivors={counts['survivors']}, live={len(live)})")
    print("\n".join(_lines + [summary]))
    if args.json:
        print(json.dumps({
            "pass": not _failures,
            "failures": _failures,
            "counts": counts,
            "live_total": len(live),
            "map": str(map_path),
            "report": str(report_path),
            "test_dir": args.test_dir,
        }, indent=2))
    return 1 if _failures else 0


if __name__ == "__main__":
    sys.exit(main())
