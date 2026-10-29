"""The protected release workflow (.github/workflows/release.yml): what it must never do with the keys, and what has to guard it.

These are text checks on the file (no YAML library is needed), written so that a later edit cannot quietly widen who can start the job or where a key goes.
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WORKFLOW = (ROOT / ".github" / "workflows" / "release.yml").read_text(encoding="utf-8").replace("\r\n", "\n")
CODE = "\n".join(line for line in WORKFLOW.split("\n") if not line.lstrip().startswith("#"))

SECRET_VARIABLES = ("URTC_SIGNING_KEYS", "URTC_RELEASE_ED25519_PEM")
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

    def test_a_published_version_is_never_rebuilt(self):
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


class WhereTheKeysGo(unittest.TestCase):
    def test_the_secrets_are_read_in_one_step_and_only_into_environment_variables(self):
        uses = re.findall(r"\$\{\{\s*secrets\.([A-Z0-9_]+)\s*\}\}", CODE)
        self.assertEqual(sorted(uses), sorted((*SECRET_VARIABLES, NOTIFY_TOKEN)))
        for name in SECRET_VARIABLES:
            self.assertRegex(CODE, rf"(?m)^          {name}: \$\{{\{{ secrets\.{name} \}}\}}$")

    def test_the_notification_token_has_a_step_of_its_own_and_is_optional(self):
        steps = re.split(r"(?m)^      - name: ", CODE)
        holders = [step for step in steps if f"secrets.{NOTIFY_TOKEN}" in step]
        self.assertEqual(len(holders), 1)
        step = holders[0]
        for name in SECRET_VARIABLES:
            self.assertNotIn(name, step)
        self.assertIn(f'[ -z "${{{NOTIFY_TOKEN}:-}}" ]', step)
        self.assertRegex(step, r"(?s)::notice::.*exit 0")
        self.assertIn("urtc-firmware-released", step)
        self.assertNotIn("exit 1", step)
        self.assertIn("repos/JuanenRac/${target}/dispatches", step)

    def test_a_key_is_only_written_to_a_file_with_printf_and_never_echoed_or_listed(self):
        for name in SECRET_VARIABLES:
            for line in CODE.split("\n"):
                if f"${{{name}}}" in line or f"${name}" in line:
                    self.assertTrue(line.lstrip().startswith(("printf '%s\\n'", 'if [ -z', "env:")), line)
        for forbidden in ("echo \"${URTC_", "cat \"${keys}", "set -x", "env |", "printenv", "::add-mask::"):
            self.assertNotIn(forbidden, CODE)

    def test_the_key_files_are_private_and_removed_whatever_happened(self):
        self.assertIn("umask 077", CODE)
        self.assertRegex(CODE, r"(?s)- name: Remove the key files\n\s+if: \$\{\{ always\(\) \}\}.*rm -rf \"\$\{RUNNER_TEMP\}/urtc-keys\"")

    def test_the_build_gets_the_keys_through_the_files_the_build_script_reads(self):
        self.assertIn('URTC_SIGNING_KEY_FILE=${keys}/signing_keys.txt', CODE)
        self.assertIn('URTC_RELEASE_KEY=${keys}/urtc_release_ed25519.pem', CODE)
        self.assertRegex(CODE, r'URTC_RELEASE: "1"')
        self.assertNotIn("URTC_CI", re.sub(r"URTC_CI=1 python3 tools/ci_validate.py", "", CODE))

    def test_nothing_is_published_unless_the_images_are_signed_with_a_production_key(self):
        self.assertIn("publish_release_assets.py --sign", CODE)
        self.assertNotIn("--allow-dev-key", CODE)
        self.assertIn('!= "official"', CODE)

    def test_the_published_files_are_checked_after_the_upload(self):
        self.assertIn("gh release download", CODE)
        self.assertIn("release_sign.py verify", CODE)


if __name__ == "__main__":
    unittest.main()
