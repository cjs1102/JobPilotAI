"""Four-place observation-source allowlist consistency (ExecPlan 0014 section 5.3).

The runtime allowlist is duplicated by contract across the Python config, the
Python provider registry, the CLI ``--provider`` choices and the C++ observation
validator. Registering a name in fewer than four places breaks the pipeline (a
config-time rejection when Python is stricter, or a runtime ``400`` from the
observation endpoint when C++ is stricter). This test fails on any drift.

It reads the C++ source read-only and skips when the repository layout is not
present (for example an installed wheel).
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

from jobpilot_sources.config import SUPPORTED_PROVIDERS
from jobpilot_sources.providers import PROVIDERS

REPO_ROOT = Path(__file__).resolve().parents[3]
CPP_SOURCE = REPO_ROOT / "service" / "src" / "job" / "source_observation.cpp"

CPP_STRING_RE = re.compile(
    r"""IsSupportedObservationSource[\s\S]{0,400}?\n    \}""",
)
CPP_NAME_RE = re.compile(r'source\s*==\s*"([^"]+)"')


class AllowlistConsistencyTest(unittest.TestCase):
    def test_python_config_and_registry_agree(self) -> None:
        self.assertEqual(
            tuple(SUPPORTED_PROVIDERS),
            tuple(module.PROVIDER for module in PROVIDERS.values()),
        )

    def test_cli_choices_come_from_the_config_tuple(self) -> None:
        from jobpilot_sources import cli

        parser = cli.build_parser()
        choices: list[str] = []

        for action in parser._actions:
            if hasattr(action, "choices") and isinstance(action.choices, dict):
                for subparser in action.choices.values():
                    for sub_action in subparser._actions:
                        if sub_action.dest == "provider":
                            choices = list(sub_action.choices or [])

        self.assertEqual(choices, list(SUPPORTED_PROVIDERS))

    @unittest.skipUnless(
        CPP_SOURCE.is_file(), "repository C++ tree is not present"
    )
    def test_cpp_allowlist_matches_the_python_allowlist_exactly(self) -> None:
        text = CPP_SOURCE.read_text(encoding="utf-8")
        block = CPP_STRING_RE.search(text)

        self.assertIsNotNone(
            block, "IsSupportedObservationSource was not found in the C++ source"
        )
        assert block is not None

        names = CPP_NAME_RE.findall(block.group(0))

        self.assertEqual(
            names,
            list(SUPPORTED_PROVIDERS),
            "C++ IsSupportedObservationSource and Python SUPPORTED_PROVIDERS "
            "must list the same source ids in the same order (four-place "
            "contract: config, providers registry, CLI choices, C++ validator)",
        )

    def test_overseas_providers_are_never_removed(self) -> None:
        for name in ("greenhouse", "lever", "ashby"):
            with self.subTest(name=name):
                self.assertIn(name, SUPPORTED_PROVIDERS)


if __name__ == "__main__":
    unittest.main()
