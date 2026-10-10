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
    normalize_host,
    normalize_platform_name,
    resolve_platform_names_path,
    source_url_host,
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

#: The canonical host-suffix mapping of the shared registry file, keyed by the
#: NORMALISED platform name (`智联招聘` -> `智联`). The C++ registry reports
#: HostMappingCount() == 18 and HostConfirmsPlatform() == 1 for every pair here.
CANONICAL_HOST_SUFFIXES = {
    "牛客": ("nowcoder.com", "niuke.com"),
    "实习僧": ("shixiseng.com",),
    "国聘": ("iguopin.com",),
    "智联": ("zhaopin.com",),
    "前程无忧": ("51job.com",),
    "中华英才": ("chinahr.com",),
    "应届生求职": ("yingjiesheng.com",),
    "大街": ("dajie.com",),
    "猎聘": ("liepin.com",),
    "拉勾": ("lagou.com",),
    "看准": ("kanzhun.com",),
    "职友集": ("jobui.com",),
    "nowcoder": ("nowcoder.com",),
    "boss直聘": ("zhipin.com",),
    "51job": ("51job.com",),
    "lagou": ("lagou.com",),
    "liepin": ("liepin.com",),
    "zhaopin": ("zhaopin.com",),
}

#: (host, platform, confirmed) captured from the compiled C++ registry through
#: `HostConfirmsPlatform(host, Normalize(platform))` - the `HC|` rows of
#: tools/verify_platform_name_parity.cpp.
HOST_CONFIRMATION_TABLE = (
    ("nowcoder.com", "牛客", True),
    ("nowcoder.com", "nowcoder", True),
    ("niuke.com", "牛客", True),
    ("niuke.com", "nowcoder", False),
    ("www.nowcoder.com", "牛客", True),
    ("WWW.NowCoder.com", "牛客", True),
    ("nowcoder.com.", "牛客", True),
    ("nowcoder.com..", "牛客", True),
    ("jobs.nowcoder.com", "牛客", True),
    ("evilnowcoder.com", "牛客", False),
    ("nowcoder.com.cn", "牛客", False),
    ("nowcoder.com:8080", "牛客", False),
    ("user@nowcoder.com", "牛客", False),
    ("zhaopin.com", "牛客", False),
    ("nowcoder.com", "智联招聘", False),
    ("zhaopin.com", "智联招聘", True),
    ("shixiseng.com", "实习僧", True),
    ("unknown.example.com", "牛客", False),
    ("nowcoder.com", "未知平台", False),
    ("", "牛客", False),
    ("nowcoder.com", "", False),
)

#: (input, normalized) for `NormalizeHost` (ASCII trim/fold, trailing dots,
#: one leading `www.`). The port stays: `HostOf` removes it before this.
HOST_NORMALIZATION_TABLE = (
    ("  NowCoder.com  ", "nowcoder.com"),
    ("WWW.NowCoder.com", "nowcoder.com"),
    ("nowcoder.com.", "nowcoder.com"),
    ("nowcoder.com...", "nowcoder.com"),
    ("www.www.x.com", "www.x.com"),
    ("nowcoder.com:8080", "nowcoder.com:8080"),
    ("　nowcoder.com　", "　nowcoder.com　"),
    ("", ""),
)

#: (url, host) for `HostOf`: authority only, userinfo and port removed; a value
#: without `://` has no host.
SOURCE_URL_HOST_TABLE = (
    ("https://nowcoder.com/campus/1001", "nowcoder.com"),
    ("http://www.nowcoder.com./a?b=1", "nowcoder.com"),
    ("https://user:pw@NowCoder.com:8443/jobs/1#frag", "nowcoder.com"),
    ("https://nowcoder.com?x=1", "nowcoder.com"),
    ("https://nowcoder.com#frag", "nowcoder.com"),
    ("nowcoder.com/campus/1001", ""),
    ("", ""),
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


class HostMappingParityTest(unittest.TestCase):
    """0015A option B: the hostname -> platform mapping, pinned to C++.

    Expectations come from the compiled C++ registry (`tools/
    verify_platform_name_parity.cpp`, `HCOUNT|`/`HC|` rows); every value below
    was produced by the real `PlatformNameRegistry`, not by reading the YAML.
    """

    def registry(self) -> PlatformNameRegistry:
        return PlatformNameRegistry.load(PACKAGE_DATA_FILE)

    def test_host_mapping_count(self) -> None:
        self.assertEqual(self.registry().host_mapping_count, 18)

    def test_canonical_host_suffixes(self) -> None:
        self.assertEqual(dict(self.registry().hosts), CANONICAL_HOST_SUFFIXES)

    def test_host_confirmation_table_matches_the_cpp_registry(self) -> None:
        registry = self.registry()

        for host, platform, expected in HOST_CONFIRMATION_TABLE:
            with self.subTest(host=host, platform=platform):
                self.assertIs(
                    registry.host_confirms_platform(
                        host, normalize_platform_name(platform)
                    ),
                    expected,
                )

    def test_host_normalization_table(self) -> None:
        for value, expected in HOST_NORMALIZATION_TABLE:
            with self.subTest(value=value):
                self.assertEqual(normalize_host(value), expected)

    def test_source_url_host_table(self) -> None:
        for url, expected in SOURCE_URL_HOST_TABLE:
            with self.subTest(url=url):
                self.assertEqual(source_url_host(url), expected)

    def test_alias_pair_shares_a_host_and_each_confirms_it(self) -> None:
        registry = self.registry()

        self.assertTrue(
            registry.host_confirms_platform("nowcoder.com", "牛客")
        )
        self.assertTrue(
            registry.host_confirms_platform("nowcoder.com", "nowcoder")
        )
        self.assertEqual(
            registry.host_suffixes("牛客"), ("nowcoder.com", "niuke.com")
        )
        self.assertEqual(
            registry.host_suffixes("nowcoder"), ("nowcoder.com",)
        )

    def test_an_alias_does_not_inherit_the_other_aliases_host(self) -> None:
        registry = self.registry()

        self.assertTrue(registry.host_confirms_platform("niuke.com", "牛客"))
        self.assertFalse(
            registry.host_confirms_platform("niuke.com", "nowcoder")
        )

    def test_alias_safety_is_structural_not_map_ordered(self) -> None:
        # Regression guard for the C++ host-keyed lookup bug: here the mapping
        # is a per-platform tuple sequence and the only lookup key is the
        # normalised platform name, so a host shared by two aliases confirms
        # both and can never resolve through order to just one of them.
        registry = self.registry()

        self.assertEqual(len(dict(registry.hosts)), registry.host_mapping_count)

        for platform in ("牛客", "nowcoder"):
            self.assertIn("nowcoder.com", registry.host_suffixes(platform))

    def test_unknown_host_and_unknown_platform_never_confirm(self) -> None:
        registry = self.registry()

        self.assertFalse(
            registry.host_confirms_platform("unknown.example.com", "牛客")
        )
        self.assertFalse(
            registry.host_confirms_platform("nowcoder.com", "未知")
        )
        self.assertFalse(registry.host_confirms_platform("", "牛客"))
        self.assertFalse(registry.host_confirms_platform("nowcoder.com", ""))

    def test_without_the_file_there_is_no_host_mapping(self) -> None:
        # Mirrors C++ FromNames(): names alone carry no host suffixes, so host
        # confirmation can only come from the single-source data file.
        bare = PlatformNameRegistry.from_names(["牛客", "nowcoder"])

        self.assertEqual(bare.host_mapping_count, 0)
        self.assertFalse(
            bare.host_confirms_platform("nowcoder.com", "牛客")
        )

        with_hosts = PlatformNameRegistry.from_names(
            ["牛客", "nowcoder"],
            hosts={
                "牛客": ("nowcoder.com", "niuke.com"),
                "nowcoder": "nowcoder.com",
            },
        )

        self.assertEqual(with_hosts.host_mapping_count, 2)
        self.assertTrue(
            with_hosts.host_confirms_platform("www.nowcoder.com", "牛客")
        )
        self.assertTrue(
            with_hosts.host_confirms_platform("nowcoder.com", "nowcoder")
        )

    def test_a_missing_file_leaves_the_host_mapping_empty(self) -> None:
        registry = PlatformNameRegistry.load(
            "/nonexistent/platform_names.yaml"
        )

        self.assertEqual(registry.host_mapping_count, 0)
        self.assertFalse(
            registry.host_confirms_platform("nowcoder.com", "牛客")
        )

    def test_the_shipped_hosts_section_uses_the_cpp_supported_grammar(self) -> None:
        """The C++ reader is line based, not a YAML parser.

        A quoted value or an inline ``# comment`` is read differently there
        (the quote/comment characters become part of the host entry and never
        match), while PyYAML strips them - i.e. the two languages would read a
        different mapping. A YAML list is ignored by both and stays allowed.
        This test fails loudly on a divergent edit instead of diverging
        silently.
        """

        lines = PACKAGE_DATA_FILE.read_text(encoding="utf-8").splitlines()
        inside = False
        checked = 0

        for line in lines:
            stripped = line.strip()

            if stripped == "platform_hosts:":
                inside = True
                continue

            if not inside:
                continue

            if not stripped or stripped.startswith("#"):
                continue

            indented = line.startswith((" ", "\t"))

            if not indented and not stripped.startswith("-"):
                break  # a new top-level key ends the section

            if stripped.startswith("-") or ":" not in stripped:
                continue

            key, _, value = stripped.partition(":")

            for part in (key, value):
                self.assertNotIn('"', part)
                self.assertNotIn("'", part)
                self.assertNotIn("#", part)

            checked += 1

        self.assertGreater(checked, 0, "platform_hosts section was not found")


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

    @staticmethod
    def canonical_host_suffixes() -> frozenset[str]:
        return frozenset(
            suffix
            for entries in CANONICAL_HOST_SUFFIXES.values()
            for suffix in entries
        )

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

    def test_no_host_suffix_is_hard_coded_in_python(self) -> None:
        offenders: list[str] = []

        for path in sorted(PACKAGE_ROOT.rglob("*.py")):
            if "tests" in path.relative_to(PACKAGE_ROOT).parts:
                continue

            text = path.read_text(encoding="utf-8")

            for suffix in self.canonical_host_suffixes():
                if suffix in text:
                    offenders.append(f"{path.name}: {suffix}")

        self.assertEqual(
            offenders,
            [],
            "host suffixes must live only in "
            "jobpilot_sources/data/platform_names.yaml",
        )

    def test_no_platform_name_is_hard_coded_in_cpp(self) -> None:
        if not any(directory.is_dir() for directory in CPP_JOB_DIRS):
            self.skipTest("C++ job sources are not present")

        offenders: list[str] = []
        tokens = CANONICAL_NORMALIZED | self.canonical_host_suffixes()

        for directory in CPP_JOB_DIRS:
            if not directory.is_dir():
                continue

            for path in sorted(directory.iterdir()):
                if path.suffix not in (".h", ".cpp"):
                    continue

                text = path.read_text(encoding="utf-8")

                for name in tokens:
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
        # The host layer is demonstrated through the real registry too.
        self.assertIn("HostConfirmsPlatform", text)
        self.assertIn("HostMappingCount", text)
        self.assertIn("platform_hosts", text)
        self.assertIn("EvaluateJobQuality", text)


if __name__ == "__main__":
    unittest.main()
