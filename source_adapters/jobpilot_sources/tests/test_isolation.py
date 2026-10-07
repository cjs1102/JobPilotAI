"""Architecture guard: the adapters must not touch the database directly.

ADR-0008 and ExecPlan 0012 require adapters to publish through the internal
C++ HTTP API only, so the package must contain no database driver imports and
no SQL statements of its own.
"""

from __future__ import annotations

import ast
import re
import sys
import unittest
from pathlib import Path

import jobpilot_sources

PACKAGE_ROOT = Path(jobpilot_sources.__file__).resolve().parent

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
}

SQL_PATTERNS = (
    re.compile(r"\bINSERT\s+INTO\b", re.IGNORECASE),
    re.compile(r"\bDELETE\s+FROM\b", re.IGNORECASE),
    re.compile(r"\bUPDATE\s+\w+\s+SET\b", re.IGNORECASE),
    re.compile(r"\bSELECT\s+[\w*,]+\s+FROM\b", re.IGNORECASE),
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
    def test_package_has_no_database_driver_imports(self) -> None:
        files = package_sources()
        self.assertTrue(files, "expected adapter sources")

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
                if root == "jobpilot_sources":
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

    def test_adapters_never_import_the_core_service_library(self) -> None:
        for path in package_sources():
            text = path.read_text(encoding="utf-8")

            self.assertNotIn("#include", text)
            self.assertNotIn("jobpilot/service", text)


if __name__ == "__main__":
    unittest.main()
