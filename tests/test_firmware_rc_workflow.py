"""The release-candidate build (.github/workflows/firmware-rc.yml): it compiles with the official trust table, holds no secret and keeps the build for days only.

These are text checks on the file (no YAML library is needed), written so that a later edit cannot quietly give the build a key, drop the second copy that
proves it reproducible or keep the artifact for long.
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WORKFLOW = (ROOT / ".github" / "workflows" / "firmware-rc.yml").read_text(encoding="utf-8").replace("\r\n", "\n")
CODE = "\n".join(line for line in WORKFLOW.split("\n") if not line.lstrip().startswith("#"))


class Triggers(unittest.TestCase):
    def test_it_is_started_by_hand_or_called_and_by_nothing_else(self):
        block = re.search(r"(?m)^on:\n((?:  .*\n|\n)+)", CODE)
        self.assertTrue(block)
        self.assertEqual(re.findall(r"(?m)^  ([a-z_]+):", block.group(1)), ["workflow_dispatch", "workflow_call"])
        for forbidden in ("pull_request", "push:", "schedule:", "workflow_run", "issue_comment"):
            self.assertNotIn(forbidden, CODE)

    def test_both_triggers_ask_for_the_version(self):
        self.assertEqual(len(re.findall(r"(?m)^      version:\n", CODE)), 2)


class NoSecret(unittest.TestCase):
    def test_no_secret_and_no_key_is_read(self):
        for forbidden in ("secrets.", "URTC_SIGNING_KEYS", "URTC_RELEASE_ED25519_PEM", "--sign", "gh release", "environment:"):
            self.assertNotIn(forbidden, CODE)
        self.assertNotRegex(CODE, r"(?m)^\s+URTC_SIGNING_KEY_FILE:")

    def test_it_can_not_write_to_the_repository(self):
        self.assertNotRegex(CODE, r"contents:\s*write")
        self.assertEqual(CODE.count("contents: read"), 2)  # the workflow and the job that attests


class Reproducible(unittest.TestCase):
    def test_the_build_uses_the_official_table_and_the_commit_date(self):
        self.assertIn('URTC_RELEASE: "1"', CODE)
        self.assertIn('URTC_CI: "1"', CODE)
        self.assertRegex(CODE, r"SOURCE_DATE_EPOCH: \$\{\{ needs\.plan\.outputs\.epoch \}\}")

    def test_it_builds_twice_and_compares_every_file(self):
        self.assertRegex(CODE, r"copy: \[a, b\]")
        self.assertIn("diff -rq rc-copy-a rc-copy-b", CODE)
        self.assertIn('"reproducible": True', CODE)

    def test_the_toolchain_is_one_pinned_release_checked_by_its_hash(self):
        self.assertRegex(CODE, r"13\.3\.rel1")
        self.assertRegex(CODE, r"[0-9a-f]{64}  \$RUNNER_TEMP/\$name\" \| sha256sum --check")

    def test_it_checks_the_versions_inside_the_images(self):
        self.assertIn("tools/check_image_versions.py build/firmware --main-version", CODE)
        self.assertIn("tools/image_trust.py check", CODE)


class ShortRetention(unittest.TestCase):
    def test_every_artifact_is_kept_for_seven_days_at_most(self):
        days = [int(n) for n in re.findall(r"retention-days: (\d+)", CODE)]
        self.assertTrue(days)
        self.assertLessEqual(max(days), 7)

    def test_the_build_has_an_attestation(self):
        self.assertIn("actions/attest-build-provenance@", CODE)


if __name__ == "__main__":
    unittest.main()
