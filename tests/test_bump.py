"""The explicit bump command: raising a version is a separate step, and compiling never does it."""
import json
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def run_in(directory, *args):
    return subprocess.run([sys.executable, "bump.py", *args], cwd=directory, capture_output=True, text=True)


def tree(directory):
    for name in ("bump.py", "bump_version.py", "bump_manifest_version.py", "urtc.project.json", "CHANGELOG.md"):
        shutil.copy(ROOT / name, directory / name)
    for header in (
        "src/F303-master/firmware_common.h", "src/F303-master/boot/bootloader_common.h",
        "src/F303-slave/slave_common.h", "src/F303-slave/boot/slaveboot_common.h",
    ):
        (directory / header).parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(ROOT / header, directory / header)


class BumpCommandTests(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self._tmp.name)
        tree(self.dir)

    def tearDown(self):
        self._tmp.cleanup()

    def versions(self):
        result = run_in(self.dir, "--list")
        self.assertEqual(result.returncode, 0, result.stderr)
        return {line.split()[0]: line.split()[2] for line in result.stdout.splitlines()}

    def test_dry_run_writes_nothing(self):
        before = self.versions()
        result = run_in(self.dir, "--dry-run")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.versions(), before)

    def test_bumping_a_bootloader_changes_only_it_and_its_manifest_entry(self):
        before = self.versions()
        result = run_in(self.dir, "slave-boot")
        self.assertEqual(result.returncode, 0, result.stderr)
        after = self.versions()
        self.assertNotEqual(after["slave-boot"], before["slave-boot"])
        self.assertEqual({k: v for k, v in after.items() if k != "slave-boot"}, {k: v for k, v in before.items() if k != "slave-boot"})
        manifest = json.loads((self.dir / "urtc.project.json").read_text(encoding="utf-8"))
        entry = next(c for c in manifest["components"] if c["name"] == "URTC_SLAVE_BOOTLOADER")
        self.assertEqual(entry["version"], after["slave-boot"])

    def test_bumping_the_main_application_moves_the_project_version_and_the_bootloader_mirror(self):
        before = json.loads((self.dir / "urtc.project.json").read_text(encoding="utf-8"))
        result = run_in(self.dir, "main-app")
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        after = json.loads((self.dir / "urtc.project.json").read_text(encoding="utf-8"))
        self.assertNotEqual(after["version"], before["version"])
        new = self.versions()["main-app"]
        self.assertEqual(after["version"], new)
        mirror = (self.dir / "src/F303-master/boot/bootloader_common.h").read_text()
        triple = ".".join(re.search(rf"#define FIRMWARE_VERSION_{p}\s+(\d+)", mirror).group(1) for p in ("MAJOR", "MINOR", "PATCH"))
        self.assertEqual(triple, new)

    def test_unknown_component_is_refused(self):
        self.assertNotEqual(run_in(self.dir, "nothing").returncode, 0)


class BuildScriptsDoNotBumpTests(unittest.TestCase):
    def test_compiling_never_runs_the_bump(self):
        for name in ("build_firmware.sh", "build_firmware.bat"):
            text = (ROOT / name).read_text(encoding="utf-8")
            self.assertNotIn("bump_manifest_version", text, name)
            for line in text.splitlines():
                if "bump_version.py" in line and not line.lstrip().lower().startswith(("#", "rem")):
                    self.assertIn("--current", line, f"{name}: {line}")


if __name__ == "__main__":
    unittest.main()
