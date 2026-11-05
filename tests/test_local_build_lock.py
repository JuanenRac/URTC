"""Nothing here compiles: the lock on builds that run on the machine of the developer (tools/local_build.py) is what is under test."""
import contextlib
import importlib.util
import io
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools" / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


lock = load("local_build")
host = load("run_host_tests")
fuzz = load("fuzz_virtual_board")
twin = load("bootloader_twin")

NO_CI = {name: "" for name in ("CI", "GITHUB_ACTIONS", "ALLOW_LOCAL_BUILD")}


class LockTests(unittest.TestCase):
    def test_the_rule(self):
        self.assertFalse(lock.local_build_allowed({}))
        self.assertTrue(lock.local_build_allowed({"CI": "true"}))
        self.assertTrue(lock.local_build_allowed({"GITHUB_ACTIONS": "true"}))
        self.assertTrue(lock.local_build_allowed({"ALLOW_LOCAL_BUILD": "1"}))
        self.assertFalse(lock.local_build_allowed({"CI": "false", "ALLOW_LOCAL_BUILD": "0"}))
        self.assertIn("ALLOW_LOCAL_BUILD=1", lock.local_build_refusal({}))

    def test_the_host_suites_are_skipped_not_passed_and_nothing_is_run(self):
        out = io.StringIO()
        with mock.patch.dict(os.environ, NO_CI), mock.patch.object(host.subprocess, "run", side_effect=AssertionError("compiled")), contextlib.redirect_stdout(out):
            self.assertEqual(host.main([]), 2)
        self.assertIn("HOST_TESTS=SKIPPED", out.getvalue())

    def test_the_virtual_boards_are_not_built(self):
        with mock.patch.dict(os.environ, NO_CI), mock.patch("subprocess.run", side_effect=AssertionError("compiled")):
            self.assertIsNone(fuzz.build(Path(tempfile.gettempdir()) / "vb"))
            self.assertIsNone(twin.build(Path(tempfile.gettempdir()) / "tw"))
            with contextlib.redirect_stdout(io.StringIO()) as out:
                self.assertEqual(fuzz.main(["--build-only", "--build-dir", tempfile.mkdtemp()]), 2)
        self.assertIn("ALLOW_LOCAL_BUILD", out.getvalue())

    def test_a_ci_runner_reaches_the_compiler_branch(self):
        with mock.patch.dict(os.environ, {**NO_CI, "CI": "true"}), mock.patch.object(fuzz.shutil, "which", return_value=None), mock.patch.object(fuzz.os, "name", "posix"):
            self.assertIsNone(fuzz.build(Path("x")))   # no compiler on this stand-in, but the lock did not stop it
            self.assertIsNone(lock.local_build_refusal(os.environ))


if __name__ == "__main__":
    unittest.main()
