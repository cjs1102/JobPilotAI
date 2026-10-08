"""Architecture guards for the AI worker package.

ADR-0009 / ExecPlan 0013 sections 6.5 and 8: the worker talks to the internal
HTTP API only - no database driver, no SQL, no C++ headers - must not add a
dependency beyond PyYAML, and must never accept a secret as a CLI flag.
"""

from __future__ import annotations

import ast
import re
import sys
import unittest
from pathlib import Path

import jobpilot_ai

PACKAGE_ROOT = Path(jobpilot_ai.__file__).resolve().parent

FORBIDDEN_MODULES = {
    "pymysql",
    "MySQLdb",
    "mysql",
    "mariadb",
    "sqlalchemy",
    "psycopg",
    "psycopg2",
    "sqlite3",
    "redis",
    "pymongo",
    "kafka",
    "confluent_kafka",
    "requests",
    "httpx",
    "aiohttp",
    "openai",
    "anthropic",
    "litellm",
    "transformers",
    "sentence_transformers",
    "torch",
    "numpy",
}

SQL_PATTERNS = (
    re.compile(r"\bINSERT\s+INTO\b", re.IGNORECASE),
    re.compile(r"\bDELETE\s+FROM\b", re.IGNORECASE),
    re.compile(r"\bUPDATE\s+\w+\s+SET\b", re.IGNORECASE),
    re.compile(r"\bSELECT\s+[\w*,]+\s+FROM\b", re.IGNORECASE),
)

SECRET_FLAG_PATTERN = re.compile(
    r"key|token|secret|password|credential",
    re.IGNORECASE,
)


def package_sources() -> list[Path]:
    return sorted(
        path
        for path in PACKAGE_ROOT.rglob("*.py")
        if "tests" not in path.relative_to(PACKAGE_ROOT).parts
    )


def imported_roots(path: Path) -> set[str]:
    tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
    roots: set[str] = set()

    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            for alias in node.names:
                roots.add(alias.name.split(".")[0])
        elif isinstance(node, ast.ImportFrom):
            if node.level == 0 and node.module:
                roots.add(node.module.split(".")[0])

    return roots


class ImportGuardTest(unittest.TestCase):
    def test_package_has_no_database_or_vendor_sdk_imports(self) -> None:
        files = package_sources()
        self.assertTrue(files, "expected worker sources")

        for path in files:
            roots = imported_roots(path)

            for forbidden in FORBIDDEN_MODULES:
                self.assertNotIn(
                    forbidden,
                    roots,
                    f"{path.name} imports {forbidden}",
                )

    def test_only_yaml_is_a_third_party_dependency(self) -> None:
        external: set[str] = set()

        for path in package_sources():
            for root in imported_roots(path):
                if root == "jobpilot_ai":
                    continue

                if root not in sys.stdlib_module_names:
                    external.add(root)

        self.assertEqual(external, {"yaml"})

    def test_package_contains_no_sql_statements(self) -> None:
        for path in package_sources():
            text = path.read_text(encoding="utf-8")

            for pattern in SQL_PATTERNS:
                self.assertIsNone(
                    pattern.search(text),
                    f"{path.name} contains SQL: {pattern.pattern}",
                )

    def test_package_never_includes_or_links_the_core_service_library(self) -> None:
        for path in package_sources():
            text = path.read_text(encoding="utf-8")

            self.assertNotIn("#include", text)
            self.assertNotIn("jobpilot/service", text)


class SecretHandlingGuardTest(unittest.TestCase):
    def test_cli_declares_no_secret_flags(self) -> None:
        tree = ast.parse(
            (PACKAGE_ROOT / "cli.py").read_text(encoding="utf-8"),
            filename="cli.py",
        )
        options: list[str] = []

        for node in ast.walk(tree):
            if not isinstance(node, ast.Call):
                continue

            function = node.func
            name = getattr(function, "attr", None)

            if name != "add_argument" or not node.args:
                continue

            for argument in node.args:
                if isinstance(argument, ast.Constant) and isinstance(
                    argument.value,
                    str,
                ):
                    options.append(argument.value)

        self.assertTrue(options)

        for option in options:
            self.assertIsNone(
                SECRET_FLAG_PATTERN.search(option),
                f"CLI option {option!r} looks like a secret input",
            )

    def test_key_material_is_handled_in_exactly_one_module(self) -> None:
        holders: set[str] = set()

        for path in package_sources():
            text = path.read_text(encoding="utf-8")

            if "Authorization" in text or "Bearer" in text:
                holders.add(path.name)

        self.assertEqual(holders, {"provider.py"})

    def test_the_internal_token_header_lives_in_the_client_only(self) -> None:
        holders = {
            path.name
            for path in package_sources()
            if "X-Internal-Token" in path.read_text(encoding="utf-8")
        }

        self.assertEqual(holders, {"client.py"})

    def test_no_source_file_mentions_a_hardcoded_secret(self) -> None:
        for path in package_sources():
            text = path.read_text(encoding="utf-8")

            self.assertNotIn("sk-", text, f"{path.name} looks like it holds a key")


class DependencyDeclarationTest(unittest.TestCase):
    def test_pyproject_declares_only_pyyaml(self) -> None:
        pyproject = (PACKAGE_ROOT.parent / "pyproject.toml").read_text(
            encoding="utf-8"
        )

        self.assertIn('"PyYAML>=6.0"', pyproject)
        self.assertNotIn("pytest", pyproject)
        self.assertNotIn("mysql", pyproject.casefold())
        self.assertNotIn("requests", pyproject)


if __name__ == "__main__":
    unittest.main()
