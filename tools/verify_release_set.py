#!/usr/bin/env python3
# =============================================================================
# URTC - tools/verify_release_set.py
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
# GPL-3.0-or-later - see LICENSE
# =============================================================================
"""Verifies a folder of signed release assets before the release workflow publishes them.

    python tools/verify_release_set.py DIR --tag v0.3.9 [--list-file FILE]

The assets are signed on the maintainer's machine (docs/RELEASE_SIGNING.md); the workflow holds no
signing key. This tool needs no secret: the public key comes from keys/registry.json (and must be
the one pinned in tools/release_sign.py), and it checks, for the files of DIR:

  * the tag is v plus the version of urtc.project.json;
  * the set is complete and consistent (the checks of tools/publish_release_assets.py: sidecar
    digests, manifests, versions, application images signed with a production key);
  * every asset has its .sig and each one verifies against the public key of the registry;
  * every image signature record (.imgsig) verifies, with no key, against the release trust table of keys/image_trust.json for the image next to
    it, and the bootloader of the set holds exactly that table (tools/verify_image_records.py);
  * no file is in DIR that the set does not account for.

Prints VERIFY_RELEASE_SET=PASS and exits 0 only when all of that holds; otherwise nothing may be
published. --list-file writes the names of the files to publish, one per line.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
import keys  # noqa: E402
import publish_release_assets  # noqa: E402
import release_sign  # noqa: E402
import verify_image_records  # noqa: E402

KEY_ID = "urtc-release-ed25519"


def registry_public_key(registry: dict | None = None) -> str:
    registry = registry or keys.load_registry()
    for entry in registry["keys"]:
        if entry["id"] == KEY_ID:
            return str(entry["public"]).lower()
    raise SystemExit(f"{KEY_ID} is not in keys/registry.json")


def verify_set(folder: Path, tag: str, with_elf: bool = False, root: Path = ROOT,
               public_hex: str | None = None, records: bool = True) -> tuple[list[str], list[str]]:
    """Returns (problems, names of the files to publish). `records` also checks every .imgsig against the release trust table (needs no key)."""
    problems: list[str] = []
    public_hex = public_hex or registry_public_key()
    if public_hex != release_sign.PUBLIC_KEY_HEX:
        problems.append("the public key of keys/registry.json is not the one pinned in tools/release_sign.py")
    if not folder.is_dir():
        return [f"{folder} is not a folder"], []
    _, version = publish_release_assets.project_info(root)
    if tag != f"v{version}":
        problems.append(f"the tag {tag} is not v{version}, the version of urtc.project.json")
    with_elf = with_elf or any(folder.glob("*.elf"))  # the .elf files are an option of the maintainer: present means part of the set
    plan = publish_release_assets.build_plan(root, folder, with_elf, None, allow_dev_key=False, sign=False)
    problems.extend(plan["problems"])
    if plan["prerelease"]:
        problems.append("the set is not signed with a production key; only an official release is published by the workflow")
    names = sorted(Path(item).name for item in plan["files"])
    # build_plan is given paths relative to the repository; with a folder elsewhere they may carry '..', the name is what counts.
    present = sorted(path.name for path in folder.iterdir() if path.is_file())
    for extra in sorted(set(present) - set(names)):
        problems.append(f"{extra} is in the folder but belongs to no component of the set")
    for name in names:
        asset = folder / name
        if name.endswith(".sig") or not asset.is_file():
            continue
        if not release_sign.verify_file(asset, public_hex=public_hex):
            problems.append(f"{name}: its signature does not verify against the public key of the registry")
    if records:
        trust_file = root / "keys" / "image_trust.json"
        doc = json.loads(trust_file.read_text(encoding="utf-8")) if trust_file.is_file() else None
        problems.extend(verify_image_records.check_folder(folder, "release", doc))
    return problems, names


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("folder")
    parser.add_argument("--tag", required=True)
    parser.add_argument("--list-file", default=None)
    args = parser.parse_args(argv)
    problems, names = verify_set(Path(args.folder), args.tag)
    for name in names:
        print(f"  {name}")
    for problem in problems:
        print(f"  PROBLEM {problem}")
    if not problems and args.list_file:
        Path(args.list_file).write_text("".join(name + "\n" for name in names), encoding="utf-8")
    print("VERIFY_RELEASE_SET=PASS" if not problems else "VERIFY_RELEASE_SET=FAIL")
    return 0 if not problems else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
