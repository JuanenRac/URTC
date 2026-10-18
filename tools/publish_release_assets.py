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

    python tools/publish_release_assets.py [--firmware-dir DIR] [--with-elf]
                                           [--repo OWNER/NAME] [--json]

Two layouts are understood:
  * firmware_manifest.json present (the URTC controller firmware): every
    component's .bin and .hex, its .sha256 and .bin.manifest.json (sha256,
    crc32 and, for application images, the hmac_sha256 the flashers transport),
    plus firmware_manifest.json itself;
  * no firmware_manifest.json: URTC_*_FIRMWARE_v<version>.bin and .hex of the
    project version; a missing .sha256 sidecar is written next to the image.
.elf files are listed only with --with-elf.
A release whose application images are signed with the public development key
is refused as an official release: it can only be published as a pre-release
("bench") with --allow-dev-key, and the printed command then carries
--prerelease. Exit status 1 when anything is missing or inconsistent.

Every asset of an official release also carries its release signature
(<asset>.sig, an Ed25519 signature by the key of the releases, see
tools/release_sign.py and docs/RELEASE_SIGNING.md). --sign writes the missing
ones with the private key of the releases (never read by this script's output);
without it the .sig files already next to the assets are checked and attached.
A pre-release may go out without signatures.
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


def collect_with_index(fw_dir: Path, with_elf: bool, problems: list[str], dev_keys: list[str] | None = None) -> list[Path]:
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
        if key.endswith("_application") and not data.get("hmac_sha256"):
            problems.append(f"{key}: application image without a published hmac_sha256")
        if key.endswith("_application") and dev_keys is not None:
            kind = data.get("signing_key_kind") or component.get("signing", {}).get("key_kind")
            if kind != "production":
                dev_keys.append(f"{key}: signed with key kind {kind!r}, not a production key")
        files.extend(path for path in wanted if path.is_file())
    files.append(fw_dir / "firmware_manifest.json")
    return files


def collect_single(fw_dir: Path, version: str, with_elf: bool, problems: list[str]) -> list[Path]:
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
    if not fw_dir.is_dir():
        problems.append(f"{fw_dir} does not exist (run build_firmware.sh without URTC_CI)")
        files: list[Path] = []
    elif (fw_dir / "firmware_manifest.json").is_file():
        files = collect_with_index(fw_dir, with_elf, problems, dev_keys)
    else:
        files = collect_single(fw_dir, version, with_elf, problems)
    prerelease = bool(dev_keys)
    if prerelease and not allow_dev_key:
        for item in dev_keys:
            problems.append(f"{item}; an official release needs production-signed images "
                            "(use --allow-dev-key to publish a bench pre-release)")
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


def gh_command(plan: dict) -> str:
    head = f'gh release create {plan["tag"]} --repo {plan["repository"]} --title "{plan["title"]}" --notes-file RELEASE_NOTES.md'
    if plan.get("prerelease"):
        head += " --prerelease"
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
    parser.add_argument("--json", action="store_true", help="print the plan as JSON instead of text")
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
            print(gh_command(plan))
    print("RELEASE_ASSETS=PASS" if not plan["problems"] else "RELEASE_ASSETS=FAIL")
    return 0 if not plan["problems"] else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
