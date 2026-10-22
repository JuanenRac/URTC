"""The manifest's evidence level: validated, and required for the established label on the tool platform."""
import contextlib
import importlib.util
import io
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
_spec = importlib.util.spec_from_file_location("urtc_ci_validate", ROOT / "tools" / "ci_validate.py")
cv = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(cv)


def _problem(manifest):
    captured = io.StringIO()
    try:
        with contextlib.redirect_stderr(captured):
            cv.validate_evidence(manifest)
    except SystemExit:
        return captured.getvalue()
    return None


class EvidenceTests(unittest.TestCase):
    def test_the_manifest_of_this_repository_passes(self):
        import json
        manifest = json.loads((ROOT / "urtc.project.json").read_text(encoding="utf-8"))
        self.assertIsNone(_problem(manifest))

    def test_a_missing_or_unknown_level_is_rejected(self):
        self.assertIn("evidence", _problem({"maturity": "functional"}))
        self.assertIn("evidence", _problem({"evidence": "tested-twice", "maturity": "functional"}))

    def test_established_on_the_tool_platform_needs_a_bench(self):
        base = {"maturity": "established", "family": "URTC Tool Platform"}
        self.assertIn("bench", _problem({**base, "evidence": "simulator"}))
        self.assertIsNone(_problem({**base, "evidence": "bench"}))
        self.assertIsNone(_problem({**base, "evidence": "field"}))

    def test_other_families_may_be_established_by_tests_alone(self):
        self.assertIsNone(_problem({"maturity": "established", "family": "Ecosystem Operations", "evidence": "host-test"}))


if __name__ == "__main__":
    unittest.main()
