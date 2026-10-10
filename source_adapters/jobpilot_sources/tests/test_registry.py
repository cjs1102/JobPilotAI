"""Cross-language platform-name parity (ExecPlan 0015A decision 4).

The C++ service and this Python package read **one** data file
(``jobpilot_sources/data/platform_names.yaml``), but each normalises it in code.
These tests pin the Python results to the values produced by the real C++
``PlatformNameRegistry`` so parity is demonstrated, not assumed.

The normalisation table below was produced by
``source_adapters/tools/verify_platform_name_parity.cpp``:

    g++ -std=c++17 -I service/include -I backend/include \
        -o /tmp/verify_platform_name_parity \
        source_adapters/tools/verify_platform_name_parity.cpp \
        service/src/job/job_quality.cpp
    cd <repository root> && /tmp/verify_platform_name_parity

The C++ ``N|...`` rows and the Python table are byte-identical (the probe also
reports ``SIZE|18``, the canonical ``SET|`` line and every ``C|``/``F|`` row).
"""

from __future__ import annotations

import os
import re
import tempfile
import unittest
from pathlib import Path

import jobpilot_sources
from jobpilot_sources.registry import (
    ASCII_WHITESPACE,
    DEFAULT_PLATFORM_NAMES_FILE,
    PACKAGE_DATA_FILE,
    PLATFORM_NAMES_FILE_ENV,
    SUFFIXES,
    PlatformNameRegistry,
    normalize_platform_name,
    resolve_platform_names_path,
)

REPO_ROOT = Path(__file__).resolve().parents[3]
PACKAGE_ROOT = Path(jobpilot_sources.__file__).resolve().parent
CPP_HEADER = (
    REPO_ROOT / "service" / "include" / "jobpilot" / "service" / "job"
    / "job_quality.h"
)
CPP_JOB_DIRS = (
    REPO_ROOT / "service" / "src" / "job",
    REPO_ROOT / "service" / "include" / "jobpilot" / "service" / "job",
)

#: The canonical normalised set of the shared registry file. Compared one-to-one
#: with the C++ registry: Load() reports Size() == 18 and Contains() == 1 for
#: every entry.
CANONICAL_NORMALIZED = frozenset(
    {
        "牛客",
        "实习僧",
        "国聘",
        "智联",
        "前程无忧",
        "中华英才",
        "应届生求职",
        "大街",
        "猎聘",
        "拉勾",
        "看准",
        "职友集",
        "nowcoder",
        "boss直聘",
        "51job",
        "lagou",
        "liepin",
        "zhaopin",
    }
)

#: (input, expected) pairs captured from the compiled C++ registry. Includes
#: trim, ASCII fold, every suffix, the repeated-strip cases and the non-ASCII
#: whitespace case that proves trimming is ASCII-only on both sides.
NORMALIZATION_TABLE = (
    (" 牛客网 ", "牛客"),
    ("牛客网", "牛客"),
    ("牛客", "牛客"),
    ("智联招聘", "智联"),
    ("中华英才网", "中华英才"),
    ("应届生求职网", "应届生求职"),
    ("大街网", "大街"),
    ("前程无忧", "前程无忧"),
    ("BOSS直聘", "boss直聘"),
    ("boss直聘", "boss直聘"),
    ("NowCoder", "nowcoder"),
    ("nowcoder", "nowcoder"),
    ("网", "网"),
    ("招聘", "招聘"),
    ("招聘网", "招聘"),
    ("xx招聘网", "xx"),
    ("abc网", "abc"),
    ("某公司", "某公司"),
    ("  ", ""),
    ("51job", "51job"),
    ("拉勾网", "拉勾"),
    ("官网", "官"),
    ("网网", "网"),
    ("人才网", "人才"),
    ("a招聘", "a"),
    ("\t牛客网\n", "牛客"),
    ("　牛客　", "　牛客　"),
    ("牛客科技", "牛客科技"),
    ("前程无忧网", "前程无忧"),
)


class NormalizationParityTest(unittest.TestCase):
    def test_normalization_table_matches_the_cpp_registry(self) -> None:
        for value, expected in NORMALIZATION_TABLE:
            with self.subTest(value=value):
                self.assertEqual(normalize_platform_name(value), expected)

    def test_canonical_normalized_set(self) -> None:
        registry = PlatformNameRegistry.load(PACKAGE_DATA_FILE)

        self.assertEqual(registry.names, CANONICAL_NORMALIZED)
        self.assertEqual(registry.size, 18)

    def test_every_canonical_name_is_contained(self) -> None:
        registry = PlatformNameRegistry.load(PACKAGE_DATA_FILE)

        for name in sorted(CANONICAL_NORMALIZED):
            with self.subTest(name=name):
                self.assertTrue(registry.contains(name))

    def test_contains_normalizes_its_argument(self) -> None:
        registry = PlatformNameRegistry.load(PACKAGE_DATA_FILE)

        self.assertTrue(registry.contains(" 牛客网 "))
        self.assertTrue(registry.contains("BOSS直聘"))
        # "智联招聘" normalises to the registered "智联".
        self.assertTrue(registry.contains("智联招聘"))

    def test_unknown_names_fail_open(self) -> None:
        registry = PlatformNameRegistry.load(PACKAGE_DATA_FILE)

        for name in ("某公司", "示例科技有限公司", "example.com",
                     "Acme Robotics", "牛客科技", ""):
            with self.subTest(name=name):
                self.assertFalse(registry.contains(name))

    def test_unicode_whitespace_is_not_trimmed_like_cpp_isspace(self) -> None:
        # C++ std::isspace in the C locale does not treat U+3000 as a space, so
        # neither does this module; the value is therefore not a platform match.
        self.assertEqual(normalize_platform_name("　牛客　"), "　牛客　")
        self.assertFalse(
            PlatformNameRegistry.load(PACKAGE_DATA_FILE).contains("　牛客　")
        )

    def test_suffixes_and_ascii_whitespace_match_the_cpp_table(self) -> None:
        self.assertEqual(SUFFIXES, ("招聘网", "人才网", "官网", "招聘", "网"))
        self.assertEqual(ASCII_WHITESPACE, " \t\n\r\v\f")

    def test_empty_registry_matches_nothing(self) -> None:
        registry = PlatformNameRegistry()

        self.assertEqual(registry.size, 0)
        self.assertFalse(registry.contains("牛客"))

    def test_from_names_normalizes_and_drops_empties(self) -> None:
        registry = PlatformNameRegistry.from_names([" 牛客网 ", "", "   ", "ACME"])

        self.assertEqual(registry.names, frozenset({"牛客", "acme"}))


class RegistryPathTest(unittest.TestCase):
    def test_constants_match_the_cpp_header(self) -> None:
        if not CPP_HEADER.is_file():
            self.skipTest("C++ header is not present")

        text = CPP_HEADER.read_text(encoding="utf-8")
        env = re.search(
            r'kPlatformNamesFileEnv\s*=\s*"([^"]+)"', text
        )
        default = re.search(
            r'kDefaultPlatformNamesFile\s*=\s*"([^"]+)"', text
        )

        self.assertIsNotNone(env, "kPlatformNamesFileEnv not found")
        self.assertIsNotNone(default, "kDefaultPlatformNamesFile not found")
        assert env is not None and default is not None

        self.assertEqual(env.group(1), PLATFORM_NAMES_FILE_ENV)
        self.assertEqual(default.group(1), DEFAULT_PLATFORM_NAMES_FILE)

    def test_the_default_is_the_repo_relative_path(self) -> None:
        self.assertEqual(
            DEFAULT_PLATFORM_NAMES_FILE,
            "source_adapters/jobpilot_sources/data/platform_names.yaml",
        )
        self.assertEqual(
            PACKAGE_DATA_FILE,
            PACKAGE_ROOT / "data" / "platform_names.yaml",
        )

    def test_repo_relative_default_resolves_from_the_repository_root(self) -> None:
        if not REPO_ROOT.is_dir():
            self.skipTest("repository root is not present")

        previous = os.getcwd()

        try:
            os.chdir(REPO_ROOT)
            resolved = resolve_platform_names_path({})
        finally:
            os.chdir(previous)

        self.assertEqual(resolved, DEFAULT_PLATFORM_NAMES_FILE)

        registry = PlatformNameRegistry.load(REPO_ROOT / DEFAULT_PLATFORM_NAMES_FILE)
        self.assertEqual(registry.names, CANONICAL_NORMALIZED)

    def test_installed_layout_falls_back_to_package_data(self) -> None:
        previous = os.getcwd()

        try:
            with tempfile.TemporaryDirectory() as directory:
                os.chdir(directory)
                resolved = resolve_platform_names_path({})
                registry = PlatformNameRegistry.load_from_environment({})
        finally:
            os.chdir(previous)

        self.assertEqual(resolved, str(PACKAGE_DATA_FILE))
        self.assertEqual(registry.names, CANONICAL_NORMALIZED)

    def test_pyproject_ships_the_registry_as_package_data(self) -> None:
        text = (REPO_ROOT / "source_adapters" / "pyproject.toml").read_text(
            encoding="utf-8"
        )

        self.assertIn('"jobpilot_sources" = ["data/*.yaml"]', text)

    def test_environment_variable_overrides_the_default(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "alt_names.yaml"
            path.write_text(
                "version: 1\nplatform_names:\n  - Acme网\n  - 牛客网\n",
                encoding="utf-8",
            )

            environment = {PLATFORM_NAMES_FILE_ENV: str(path)}

            self.assertEqual(resolve_platform_names_path(environment), str(path))

            registry = PlatformNameRegistry.load_from_environment(environment)

        self.assertEqual(registry.names, frozenset({"acme", "牛客"}))
        self.assertEqual(registry.source_path, str(path))

    def test_blank_environment_variable_is_ignored(self) -> None:
        resolved = resolve_platform_names_path({PLATFORM_NAMES_FILE_ENV: "   "})

        self.assertNotEqual(resolved, "   ")
        self.assertTrue(resolved.endswith("platform_names.yaml"))


class FailOpenTest(unittest.TestCase):
    def test_missing_file_warns_and_matches_nothing(self) -> None:
        registry = PlatformNameRegistry.load("/nonexistent/platform_names.yaml")

        self.assertEqual(registry.size, 0)
        self.assertIn("unavailable", registry.warning)
        self.assertFalse(registry.contains("牛客"))

    def test_invalid_yaml_warns_and_matches_nothing(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "broken.yaml"
            path.write_text("platform_names: [unclosed", encoding="utf-8")

            registry = PlatformNameRegistry.load(path)

        self.assertEqual(registry.size, 0)
        self.assertIn("not valid YAML", registry.warning)

    def test_missing_key_warns_and_matches_nothing(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "empty.yaml"
            path.write_text("version: 1\n", encoding="utf-8")

            registry = PlatformNameRegistry.load(path)

        self.assertEqual(registry.size, 0)
        self.assertIn("no platform_names entries", registry.warning)

    def test_quoted_and_commented_entries_load_like_cpp(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "quoted.yaml"
            path.write_text(
                "# comment\n"
                "version: 1\n"
                "platform_names:\n"
                '  - "牛客网"\n'
                "  - '榜样网'  # trailing comment\n"
                "  - 实习僧\n",
                encoding="utf-8",
            )

            registry = PlatformNameRegistry.load(path)

        self.assertEqual(registry.names, frozenset({"牛客", "榜样", "实习僧"}))


class SingleSourceGuardTest(unittest.TestCase):
    """No second hard-coded platform list in either language."""

    def test_no_platform_name_is_hard_coded_in_python(self) -> None:
        offenders: list[str] = []

        for path in sorted(PACKAGE_ROOT.rglob("*.py")):
            if "tests" in path.relative_to(PACKAGE_ROOT).parts:
                continue

            text = path.read_text(encoding="utf-8")

            for name in CANONICAL_NORMALIZED:
                if name in text:
                    offenders.append(f"{path.name}: {name}")

        self.assertEqual(
            offenders,
            [],
            "platform names must live only in "
            "jobpilot_sources/data/platform_names.yaml",
        )

    def test_no_platform_name_is_hard_coded_in_cpp(self) -> None:
        if not any(directory.is_dir() for directory in CPP_JOB_DIRS):
            self.skipTest("C++ job sources are not present")

        offenders: list[str] = []

        for directory in CPP_JOB_DIRS:
            if not directory.is_dir():
                continue

            for path in sorted(directory.iterdir()):
                if path.suffix not in (".h", ".cpp"):
                    continue

                text = path.read_text(encoding="utf-8")

                for name in CANONICAL_NORMALIZED:
                    if name in text:
                        offenders.append(f"{path.name}: {name}")

        self.assertEqual(
            offenders,
            [],
            "the C++ engine must read the shared registry file, never a "
            "second hard-coded list",
        )

    def test_the_parity_probe_is_committed(self) -> None:
        probe = (
            REPO_ROOT / "source_adapters" / "tools"
            / "verify_platform_name_parity.cpp"
        )

        self.assertTrue(probe.is_file())
        text = probe.read_text(encoding="utf-8")

        self.assertIn("PlatformNameRegistry::Normalize", text)
        self.assertIn("platform_names.yaml", text)


if __name__ == "__main__":
    unittest.main()
