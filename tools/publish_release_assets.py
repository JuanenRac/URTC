#!/usr/bin/env python3
# =============================================================================
# URTC - tools/publish_release_assets.py
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
# GPL-3.0-or-later - see LICENSE
# =============================================================================
"""List the firmware files to attach to the GitHub release of this version.

Firmware images are not kept in git: a release build writes them to firmware/
(ignored) and they are published only as assets of the release of their
version. This script checks that the set in the firmware folder is complete
and consistent with urtc.project.json, then prints the exact
`gh release create` command. It never runs gh and never changes a tracked file.
With --draft the command it prints uploads the signed set as an unpublished draft, which
.github/workflows/release.yml (tools/verify_release_set.py) verifies and publishes.

    python tools/publish_release_assets.py [--firmware-dir DIR] [--with-elf]
                                           [--repo OWNER/NAME] [--json]

Two layouts are understood:
  * firmware_manifest.json present (the URTC controller firmware): every
    component's .bin and .hex, its .sha256 and .bin.manifest.json (sha256,
    crc32 and, for application images, the hmac_sha256 of the earlier generation of bootloaders when the build has one, and the .imgsig record),
    plus firmware_manifest.json itself;
  * no firmware_manifest.json: URTC_*_FIRMWARE_v<version>.bin and .hex of the
    project version; a missing .sha256 sidecar is written next to the image.
.elf files are listed only with --with-elf.
A release whose application images are signed with the public development key
(signing_key_kind "dev") is refused as an official release: it can only be published as a pre-release
("bench") with --allow-dev-key, and the printed command then carries
--prerelease. Exit status 1 when anything is missing or inconsistent.

Every asset of an official release also carries its release signature
(<asset>.sig, an Ed25519 signature by the key of the releases, see
tools/release_sign.py and docs/RELEASE_SIGNING.md). --sign writes the missing
ones with the private key of the releases (never read by this script's output);
without it the .sig files already next to the assets are checked and attached.
A pre-release may go out without signatures.

Every application image of an official release also has its image signature record, <image>.imgsig (the 76-byte Ed25519 record, as 152
hexadecimal digits on one line, that the bootloaders verify: docs/SIGNATURE_SEPARATION.md of URTC-COMMON). It is made after the build
and apart from it, by the signing tool on the maintainer's machine, and goes in the set like any other asset (with its own release
signature). The bootloader files carry none. A bench pre-release may go out without them; an official release is refused without.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_OWNER = "JuanenRac"
#: The values of signing_key_kind that mean "a private key that is not in the repository".
PRODUCTION_KEY_KINDS = ("release", "production")
#: An image signature record: 76 bytes as exactly 152 hexadecimal digits on one line (at most one line end after it).
IMGSIG_PATTERN = re.compile(r"[0-9a-fA-F]{152}\n?")

sys.path.insert(0, str(Path(__file__).resolve().parent))
import release_sign


def sha256_of(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def project_info(root: Path) -> tuple[str, str]:
    manifest = json.loads((root / "urtc.project.json").read_text(encoding="utf-8"))
    return str(manifest["name"]), str(manifest["version"])


def repo_slug(root: Path, name: str) -> str:
    try:
        url = subprocess.run(
            ["git", "config", "--get", "remote.origin.url"], cwd=root,
            capture_output=True, text=True, check=True,
        ).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        url = ""
    match = re.search(r"github\.com[:/]+([^/]+)/([^/]+?)(?:\.git)?$", url)
    return f"{match.group(1)}/{match.group(2)}" if match else f"{DEFAULT_OWNER}/{name}"


def image_record(image: Path, files: list[Path], problems: list[str], missing: list[str] | None, label: str) -> None:
    """Adds the <image>.imgsig of an application image to `files` when it is there and well formed; otherwise `label` goes to `missing`."""
    record = Path(str(image) + ".imgsig")
    if not record.is_file():
        if missing is not None:
            missing.append(label)
        return
    text = record.read_bytes().decode("ascii", errors="replace")
    if not IMGSIG_PATTERN.fullmatch(text):
        problems.append(f"{record.name} is not an image signature record (152 hexadecimal digits on one line)")
        return
    files.append(record)


def collect_with_index(fw_dir: Path, with_elf: bool, problems: list[str], dev_keys: list[str] | None = None,
                       missing: list[str] | None = None) -> list[Path]:
    index = json.loads((fw_dir / "firmware_manifest.json").read_text(encoding="utf-8"))
    files: list[Path] = []
    for key, component in index.get("components", {}).items():
        bin_name = component["files"]["bin"]["filename"]
        image = fw_dir / bin_name
        sidecar = fw_dir / (bin_name + ".sha256")
        manifest = fw_dir / (bin_name + ".manifest.json")
        wanted = [image, fw_dir / (Path(bin_name).stem + ".hex"), sidecar, manifest]
        if with_elf:
            wanted.append(fw_dir / (Path(bin_name).stem + ".elf"))
        for path in wanted:
            if not path.is_file():
                problems.append(f"{key}: {path.name} is missing")
        if not (image.is_file() and sidecar.is_file() and manifest.is_file()):
            continue
        actual = sha256_of(image)
        if not sidecar.read_text(encoding="utf-8").startswith(actual):
            problems.append(f"{key}: {sidecar.name} does not match {image.name}")
        data = json.loads(manifest.read_text(encoding="utf-8"))
        if data.get("sha256") != actual:
            problems.append(f"{key}: sha256 in {manifest.name} does not match {image.name}")
        if component["files"]["bin"].get("sha256") != actual:
            problems.append(f"{key}: sha256 in firmware_manifest.json does not match {image.name}")
        if data.get("version") != component.get("version_string"):
            problems.append(f"{key}: version in {manifest.name} differs from firmware_manifest.json")
        if "_v" + str(component.get("version_string")) + "." not in bin_name:
            problems.append(f"{key}: file name {bin_name} does not carry version {component.get('version_string')}")
        # An image carries the signature its bootloader checks: the Ed25519 record (<image>.imgsig) or, for the bootloaders of the earlier generation, the hmac_sha256 of its manifest.
        if key.endswith("_application") and not data.get("hmac_sha256") and not (fw_dir / (bin_name + ".imgsig")).is_file():
            problems.append(f"{key}: application image with neither a signature record ({bin_name}.imgsig) nor a published hmac_sha256")
        if key.endswith("_application") and dev_keys is not None:
            kind = data.get("signing_key_kind") or component.get("signing", {}).get("key_kind")
            # The build writes "release" for a key read from URTC_SIGNING_KEY_FILE and "dev" for the public development key;
            # "production" is the older name of the first one.
            if kind not in PRODUCTION_KEY_KINDS:
                dev_keys.append(f"{key}: signed with key kind {kind!r}, not a production key")
        files.extend(path for path in wanted if path.is_file())
        if key.endswith("_application"):
            image_record(image, files, problems, missing, f"{key}: {bin_name}.imgsig")
    files.append(fw_dir / "firmware_manifest.json")
    return files


def collect_single(fw_dir: Path, version: str, with_elf: bool, problems: list[str], missing: list[str] | None = None) -> list[Path]:
    images = sorted(fw_dir.glob(f"URTC_*_FIRMWARE_v{version}.bin"))
    if not images:
        problems.append(f"no URTC_*_FIRMWARE_v{version}.bin in {fw_dir} (run build_firmware.sh without URTC_CI)")
        return []
    files: list[Path] = []
    for image in images:
        sidecar = fw_dir / (image.name + ".sha256")
        digest = sha256_of(image)
        if not sidecar.is_file():
            sidecar.write_text(f"{digest}  {image.name}\n", encoding="utf-8")
        elif not sidecar.read_text(encoding="utf-8").startswith(digest):
            problems.append(f"{sidecar.name} does not match {image.name}")
        wanted = [image, fw_dir / (image.stem + ".hex"), sidecar]
        if with_elf:
            wanted.append(fw_dir / (image.stem + ".elf"))
        for path in wanted:
            if not path.is_file():
                problems.append(f"{path.name} is missing")
        files.extend(path for path in wanted if path.is_file())
        image_record(image, files, problems, missing, f"{image.name}.imgsig")
    return files


def attach_signatures(files: list[Path], sign: bool, official: bool, problems: list[str]) -> list[Path]:
    """Adds the <asset>.sig of every asset: written first when `sign`, checked against the pinned release key when there. An official release needs all of them."""
    result: list[Path] = []
    key = None
    for path in files:
        result.append(path)
        signature = Path(str(path) + ".sig")
        if sign:
            key = key or release_sign._private_key()
            release_sign.sign_file(path, key)
        if signature.is_file():
            if release_sign.verify_file(path):
                result.append(signature)
            else:
                problems.append(f"{signature.name} is not a valid release signature of {path.name}")
        elif official:
            problems.append(f"{path.name} has no {signature.name} (run with --sign, or sign it with tools/release_sign.py)")
    return result


def build_plan(root: Path, fw_dir: Path, with_elf: bool, repo: str | None, allow_dev_key: bool = False, sign: bool = False) -> dict:
    name, version = project_info(root)
    problems: list[str] = []
    dev_keys: list[str] = []
    missing_records: list[str] = []
    if not fw_dir.is_dir():
        problems.append(f"{fw_dir} does not exist (run build_firmware.sh without URTC_CI)")
        files: list[Path] = []
    elif (fw_dir / "firmware_manifest.json").is_file():
        files = collect_with_index(fw_dir, with_elf, problems, dev_keys, missing_records)
    else:
        files = collect_single(fw_dir, version, with_elf, problems)
    prerelease = bool(dev_keys)
    if prerelease and not allow_dev_key:
        for item in dev_keys:
            problems.append(f"{item}; an official release needs production-signed images "
                            "(use --allow-dev-key to publish a bench pre-release)")
    if not prerelease:
        for item in missing_records:
            problems.append(f"{item} is missing: an official release carries the image signature record of every application image "
                            "(make it with the signing tool of the maintainer, docs/RELEASE_SIGNING.md)")
    files = attach_signatures(files, sign, not prerelease, problems)
    slug = repo or repo_slug(root, name)
    tag = f"v{version}"
    return {
        "repository": slug,
        "tag": tag,
        "title": f"{name} {tag}",
        "prerelease": prerelease,
        "files": [Path(os.path.relpath(path, root)).as_posix() for path in files],
        "problems": problems,
    }


def gh_command(plan: dict, draft: bool = False) -> str:
    head = f'gh release create {plan["tag"]} --repo {plan["repository"]} --title "{plan["title"]}" --notes-file RELEASE_NOTES.md'
    if plan.get("prerelease"):
        head += " --prerelease"
    if draft:
        # A draft is not public: it only holds the signed files for the release workflow, which verifies them and publishes.
        head += " --draft"
    continuation = " " + chr(92) + chr(10)
    return continuation.join([head] + [f'  "{path}"' for path in plan["files"]])


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--firmware-dir", default=None, help="folder with the built images (default: firmware/)")
    parser.add_argument("--with-elf", action="store_true", help="also attach the .elf files")
    parser.add_argument("--repo", default=None, help="OWNER/NAME of the GitHub repository (default: from the origin remote)")
    parser.add_argument("--allow-dev-key", action="store_true",
                        help="allow images signed with the public development key, as a pre-release only")
    parser.add_argument("--sign", action="store_true", help="write the release signature (<asset>.sig) of every asset with the key of the releases")
    parser.add_argument("--draft", action="store_true",
                        help="print the gh command with --draft: the signed set is uploaded unpublished and the release workflow verifies and publishes it")
    parser.add_argument("--json", action="store_true", help="print the plan as JSON instead of text")
    parser.add_argument("--list-file", default=None, metavar="FILE",
                        help="when nothing is wrong, write the assets to FILE, one path per line (for a job that publishes them)")
    parser.add_argument("--tag-file", default=None, metavar="FILE",
                        help="when nothing is wrong, write the tag, and on a second line 'prerelease' or 'official', to FILE")
    args = parser.parse_args(argv)
    fw_dir = Path(args.firmware_dir) if args.firmware_dir else ROOT / "firmware"
    plan = build_plan(ROOT, fw_dir, args.with_elf, args.repo, args.allow_dev_key, args.sign)
    if args.json:
        print(json.dumps(plan, indent=2))
    else:
        print(f'Release {plan["tag"]} of {plan["repository"]}: {len(plan["files"])} asset(s)')
        for path in plan["files"]:
            print(f"  {path}")
        for problem in plan["problems"]:
            print(f"  PROBLEM {problem}")
        if not plan["problems"]:
            print()
            print(gh_command(plan, args.draft))
    if not plan["problems"]:
        if args.list_file:
            Path(args.list_file).write_text("".join(path + "\n" for path in plan["files"]), encoding="utf-8")
        if args.tag_file:
            Path(args.tag_file).write_text(plan["tag"] + "\n" + ("prerelease" if plan["prerelease"] else "official") + "\n", encoding="utf-8")
    print("RELEASE_ASSETS=PASS" if not plan["problems"] else "RELEASE_ASSETS=FAIL")
    return 0 if not plan["problems"] else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
