"""The test count in the At a glance table and in the badge of the eight READMEs is the number of tests this suite really has."""
import json
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
READMES = ["README.md", "README_spa.md", "README_deu.md", "README_fra.md", "README_ita.md", "README_jpn.md", "README_por.md", "README_zho.md"]
LABELS = {"Tests", "Pruebas", "Test", "Testes", "テスト", "测试"}


def discovered() -> int:
    return unittest.TestLoader().discover(str(ROOT / "tests"), pattern="test_*.py").countTestCases()


def host_suites() -> int:
    return len(json.loads((ROOT / "tools" / "host_tests.json").read_text(encoding="utf-8"))["suites"])


def glance_row(text: str) -> str:
    for line in text.splitlines():
        cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
        if line.startswith("|") and len(cells) == 2 and cells[0] in LABELS:
            return cells[1]
    return ""


class ReadmeTestCountTests(unittest.TestCase):
    def test_glance_row_and_badge_state_the_real_python_test_count(self):
        real = discovered()
        for name in READMES:
            text = (ROOT / name).read_text(encoding="utf-8")
            row = glance_row(text)
            self.assertTrue(row, f"{name}: no Tests row in At a glance")
            self.assertEqual(int(re.match(r"\d+", row).group(0)), real, name)
            self.assertIn(f"badge/Tests-{real}%20tests", text, name)

    def test_glance_row_names_the_real_number_of_c_host_suites(self):
        real = host_suites()
        for name in READMES:
            row = glance_row((ROOT / name).read_text(encoding="utf-8"))
            self.assertIn(f"{real} ", row.split("),", 1)[-1] if ")," in row else row, name)

    def test_the_row_is_readable_by_the_dashboard_parser(self):
        row = glance_row((ROOT / "README.md").read_text(encoding="utf-8"))
        self.assertRegex(row, r"^\d+ \(`python -m unittest discover -s tests`\)")


if __name__ == "__main__":
    unittest.main()
