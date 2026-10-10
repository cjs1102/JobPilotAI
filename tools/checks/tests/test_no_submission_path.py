"""D5 guard tests: the anti-submission check must be non-vacuous.

The real tree must be clean, AND a synthetic violation must be detected —
otherwise the check would pass even if an auto-apply path were added.
"""

import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import no_submission_path  # noqa: E402

REPO_ROOT = os.path.dirname(
    os.path.dirname(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    )
)


class RealTreeTest(unittest.TestCase):
    def test_no_submission_or_auto_apply_path_exists(self):
        findings = no_submission_path.scan(REPO_ROOT)

        self.assertEqual(
            findings,
            [],
            "\n".join(
                "%s:%d %s" % (path, number, token)
                for path, number, token, _ in findings
            ),
        )

    def test_main_reports_success_for_the_real_tree(self):
        self.assertEqual(no_submission_path.main(["--root", REPO_ROOT]), 0)


class DetectionTest(unittest.TestCase):
    def write(self, directory, name, text):
        path = os.path.join(directory, name)

        with open(path, "w", encoding="utf-8") as handle:
            handle.write(text)

        return path

    def test_detects_a_synthetic_auto_apply_identifier(self):
        with tempfile.TemporaryDirectory() as directory:
            self.write(
                directory,
                "service.cpp",
                "void auto_apply_to_employer() {}\n",
            )

            findings = no_submission_path.scan(directory)

            self.assertEqual(len(findings), 1)
            self.assertEqual(findings[0][2], "auto_apply")

    def test_detects_a_synthetic_submission_route(self):
        with tempfile.TemporaryDirectory() as directory:
            self.write(
                directory,
                "router.cpp",
                '        Register("POST", "/api/v1/resumes/{id}/submit", ...);\n',
            )

            findings = no_submission_path.scan(directory)

            self.assertEqual(len(findings), 1)
            self.assertEqual(findings[0][2], '/submit"')

    def test_documentation_prose_is_out_of_scope(self):
        with tempfile.TemporaryDirectory() as directory:
            os.makedirs(os.path.join(directory, "docs"))
            self.write(
                os.path.join(directory, "docs"),
                "adr.md",
                "We will never auto-apply or auto-submit on the user's behalf.\n",
            )

            self.assertEqual(
                no_submission_path.scan(directory),
                [],
            )

    def test_main_fails_on_a_synthetic_violation(self):
        with tempfile.TemporaryDirectory() as directory:
            self.write(
                directory,
                "worker.py",
                "def submit_application(job):\n    pass\n",
            )

            self.assertEqual(
                no_submission_path.main(["--root", directory]),
                1,
            )


if __name__ == "__main__":
    unittest.main()
