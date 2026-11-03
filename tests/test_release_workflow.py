"""The release workflow (.github/workflows/release.yml): it only verifies and publishes what was signed on the maintainer's machine.

These are text checks on the file (no YAML library is needed), written so that a later edit cannot quietly widen who can start the job, give it a signing key or publish something that was not verified.
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WORKFLOW = (ROOT / ".github" / "workflows" / "release.yml").read_text(encoding="utf-8").replace("\r\n", "\n")
CODE = "\n".join(line for line in WORKFLOW.split("\n") if not line.lstrip().startswith("#"))

# the optional token that tells the flasher and the web studio about a release: it is not a key and has a step of its own
NOTIFY_TOKEN = "URTC_DISPATCH_TOKEN"


def triggers() -> list[str]:
    block = re.search(r"(?m)^on:\n((?:  .*\n|\n)+)", CODE)
    assert block, "no on: block"
    return re.findall(r"(?m)^  ([a-z_]+):", block.group(1))


class WhoCanStartIt(unittest.TestCase):
    def test_it_is_started_by_hand_and_by_nothing_else(self):
        self.assertEqual(triggers(), ["workflow_dispatch"])
        for forbidden in ("pull_request_target", "pull_request:", "push:", "schedule:", "workflow_run", "issue_comment"):
            self.assertNotIn(forbidden, CODE)

    def test_it_asks_for_the_tag_it_will_publish(self):
        self.assertRegex(CODE, r"(?m)^      tag:\n")
        self.assertIn('"${REQUESTED_TAG}" != "v${version}"', CODE)

    def test_the_job_runs_from_main_only_and_inside_the_protected_environment(self):
        self.assertIn("if: ${{ github.ref == 'refs/heads/main' }}", CODE)
        self.assertRegex(CODE, r"(?m)^    environment: release$")

    def test_a_published_version_is_never_replaced(self):
        self.assertIn('gh release view "${REQUESTED_TAG}"', CODE)


class WhatItMayDo(unittest.TestCase):
    def test_read_only_by_default_and_write_only_to_contents_in_the_job(self):
        self.assertRegex(CODE, r"(?m)^permissions:\n  contents: read\n")
        self.assertRegex(CODE, r"(?m)^    permissions:\n      contents: write\n")
        self.assertEqual(len(re.findall(r"(?m)^\s+[a-z-]+: write$", CODE)), 1)

    def test_every_action_is_pinned_by_commit(self):
        uses = re.findall(r"(?m)^\s+uses: (\S+)", CODE)
        self.assertTrue(uses)
        for item in uses:
            self.assertRegex(item, r"^[\w./-]+@[0-9a-f]{40}$", item)

    def test_the_checkout_does_not_keep_the_token_in_the_repository(self):
        self.assertIn("persist-credentials: false", CODE)


class NoKeysAnywhere(unittest.TestCase):
    def test_the_only_secret_is_the_optional_notification_token(self):
        uses = re.findall(r"\$\{\{\s*secrets\.([A-Z0-9_]+)\s*\}\}", CODE)
        self.assertEqual(uses, [NOTIFY_TOKEN])

    def test_nothing_signs_builds_or_reads_a_private_key(self):
        for forbidden in ("URTC_SIGNING_KEYS", "URTC_RELEASE_ED25519_PEM", "URTC_SIGNING_KEY_FILE", "URTC_RELEASE_KEY", "--sign",
                          "release_sign.py sign", "build_firmware", "gcc-arm-none-eabi", "cryptography", ".pem", "printenv", "set -x"):
            self.assertNotIn(forbidden, CODE)

    def test_the_notification_token_has_a_step_of_its_own_and_is_optional(self):
        steps = re.split(r"(?m)^      - name: ", CODE)
        holders = [step for step in steps if f"secrets.{NOTIFY_TOKEN}" in step]
        self.assertEqual(len(holders), 1)
        step = holders[0]
        self.assertIn(f'[ -z "${{{NOTIFY_TOKEN}:-}}" ]', step)
        self.assertRegex(step, r"(?s)::notice::.*exit 0")
        self.assertIn("urtc-firmware-released", step)
        self.assertNotIn("exit 1", step)


class WhatItChecksBeforePublishing(unittest.TestCase):
    def positions(self):
        verify = CODE.index("tools/verify_release_set.py")
        publish = CODE.index("gh release create")
        return verify, publish

    def test_the_files_are_verified_before_the_release_is_created(self):
        verify, publish = self.positions()
        self.assertLess(verify, publish)
        self.assertIn("tools/keys.py verify", CODE)

    def test_the_set_is_stopped_by_the_first_failure(self):
        self.assertIn("set -euo pipefail", CODE)
        self.assertNotIn("|| true", CODE)
        self.assertNotIn("continue-on-error", CODE)

    def test_the_release_is_made_from_the_verified_copy_not_from_the_draft(self):
        self.assertIn('cd "${RUNNER_TEMP}/signed"', CODE)
        self.assertIn('mapfile -t assets < "${RUNNER_TEMP}/assets.txt"', CODE)
        self.assertNotIn("--draft=false", CODE)

    def test_exactly_one_draft_of_the_tag_is_the_source(self):
        self.assertIn(".draft and .tag_name", CODE)
        self.assertIn('!= "1"', CODE)

    def test_the_published_files_are_checked_again_after_the_upload(self):
        self.assertIn("gh release download", CODE)
        self.assertEqual(CODE.count("tools/verify_release_set.py"), 2)


if __name__ == "__main__":
    unittest.main()
