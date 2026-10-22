"""Tests for tools/_tracked_names.py and for the WSL path translation of tools/run_host_tests.py."""
import importlib.util
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools" / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


tn = load("_tracked_names")
rht = load("run_host_tests")


class NameTests(unittest.TestCase):
    def test_names_that_come_from_an_untranslated_windows_path_are_found(self):
        bad = ["C\uf03a/Users/x/host_tests_a", "C:/Users/x/b", "Users/juane/AppData/x", "C/Users/x", "docs/a:b.txt", "d\uf03a/x"]
        self.assertEqual(tn.bad_names(bad), bad)

    def test_ordinary_names_are_fine(self):
        good = ["README.md", "src/F303-master/boot/a.c", "docs/CANBUS.TXT", "Cargo.toml", "CHANGELOG.md", "tests/C_test.c", "docs/Users_guide.md", "build.sh"]
        self.assertEqual(tn.bad_names(good), [])

    def test_a_repository_with_such_a_file_fails_and_one_without_passes(self):
        with tempfile.TemporaryDirectory() as folder:
            repo = Path(folder)
            subprocess.run(["git", "init", "-q"], cwd=repo, check=True)
            (repo / "a.txt").write_text("x", encoding="utf-8")
            (repo / "Users").mkdir()
            (repo / "Users" / "b.bin").write_text("x", encoding="utf-8")
            subprocess.run(["git", "add", "-A"], cwd=repo, check=True)
            self.assertIn("Windows path", tn.check_tracked_names(repo) or "")
            subprocess.run(["git", "rm", "-r", "-q", "--cached", "Users"], cwd=repo, check=True)
            self.assertIsNone(tn.check_tracked_names(repo))

    def test_this_repository_is_clean(self):
        self.assertIsNone(tn.check_tracked_names(ROOT))


class WslPathTests(unittest.TestCase):
    def test_a_windows_folder_is_translated_for_wsl(self):
        self.assertEqual(rht.wsl_path(r"C:\Users\x\Temp\urtc_host"), "/mnt/c/Users/x/Temp/urtc_host")
        self.assertEqual(rht.wsl_path("D:/a/b"), "/mnt/d/a/b")
        self.assertEqual(rht.wsl_path("/tmp/x"), "/tmp/x")
        self.assertEqual(rht.wsl_path("build"), "build")

    def test_only_the_build_dir_argument_is_translated(self):
        self.assertEqual(
            rht.translate_arguments(["--only", "multinode", "--build-dir", r"C:\t\h", "--no-analyze"]),
            ["--only", "multinode", "--build-dir", "/mnt/c/t/h", "--no-analyze"],
        )
        self.assertEqual(rht.translate_arguments([r"--build-dir=C:\t\h"]), ["--build-dir=/mnt/c/t/h"])

    def test_a_windows_path_on_a_linux_host_is_refused_instead_of_creating_a_folder(self):
        if not sys.platform.startswith("linux"):
            self.skipTest("the refusal is on the Linux side")
        self.assertEqual(rht.main(["--build-dir", r"C:\x\y"]), 1)


if __name__ == "__main__":
    unittest.main()
