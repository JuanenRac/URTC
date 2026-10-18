# Firmware distribution

Firmware binaries are **not** versioned in git. `URTC`, `URTC-SMART-RACK` and
`URTC-VISION-TOOL` publish them only as **assets of the GitHub release** of
each version. This page says where the files come from now and what each
consumer has to change to read them.

## What changed

- `firmware/*.bin`, `*.elf`, `*.hex`, `*.sha256`, `*.manifest.json` and
  `firmware_manifest.json` are git-ignored. They are no longer in the
  repository, so they are not on `raw.githubusercontent.com` and not in the
  GitHub contents API listing of `firmware/`.
- `build_firmware.sh` / `build_firmware.bat` in release mode (no
  `HYDRA_UMC_CI`) write the images to `firmware/` locally. In CI mode
  (`HYDRA_UMC_CI=1`) they write to `build/firmware` and touch no tracked file.
- `python tools/publish_release_assets.py` checks the local set (digests,
  versions, signatures present) and prints the exact `gh release create`
  command with every file to attach. It never runs `gh`.

## The assets of a release

URTC (tag `v<main application version>`, for example `v0.3.5`), 17 files, or
21 with `--with-elf`:

| Component | Files |
| --- | --- |
| main bootloader | `URTC_MAIN_BOOTLOADER_v<ver>.bin`, `.hex`, `.bin.sha256`, `.bin.manifest.json` |
| main application | `URTC_MAIN_FIRMWARE_v<ver>.bin`, `.hex`, `.bin.sha256`, `.bin.manifest.json` |
| slave bootloader | `URTC_SLAVE_BOOTLOADER_v<ver>.bin`, `.hex`, `.bin.sha256`, `.bin.manifest.json` |
| slave application | `URTC_SLAVE_FIRMWARE_v<ver>.bin`, `.hex`, `.bin.sha256`, `.bin.manifest.json` |
| index | `firmware_manifest.json` |

The two application manifests carry `hmac_sha256` (the signature the
bootloader expects), `signing_key_kind` and `signing_key_id`; every manifest
carries `sha256`, `crc32`, `size_bytes` and `hardware_id`.

URTC-SMART-RACK and URTC-VISION-TOOL: `URTC_SMART_RACK_FIRMWARE_v<ver>.bin`,
`.hex`, `.bin.sha256` (and `.elf` with `--with-elf`); likewise
`URTC_VISION_TOOL_FIRMWARE_v<ver>...`.

The file names carry their own version, so a name never changes meaning
between releases. `firmware_manifest.json` is the fixed-name entry point.

## Reading the assets

- Fixed-name file of the newest release, no API call and no rate limit:
  `https://github.com/JuanenRac/URTC/releases/latest/download/firmware_manifest.json`
- Any other file of the newest release, by the names listed in that manifest
  (`components.<name>.files.bin.filename` and the `.sha256` /
  `.bin.manifest.json` siblings):
  `https://github.com/JuanenRac/URTC/releases/latest/download/<filename>`
- A specific version:
  `https://github.com/JuanenRac/URTC/releases/download/v<ver>/<filename>`
- Listing with sizes and digests: `GET https://api.github.com/repos/JuanenRac/URTC/releases/latest`
  (`assets[].name`, `size`, `browser_download_url`, `digest`).

All of these answer with a redirect to GitHub's asset host
(`objects.githubusercontent.com`), so a client that restricts download hosts
must allow it after the redirect.

## How each consumer reads them

- **URTC-FLASHER**: `flasher_github.py` lists the assets of a release through
  the Releases API (`/releases/latest`, or `/releases/tags/<tag>`); downloads
  are accepted only from `github.com` (this project's release paths) and
  `objects.githubusercontent.com`, checked before the request and after
  redirects. `tools/sync_firmware.py` downloads a release
  (`--release <tag>`) into a temporary folder and runs the same checks
  (sha256 against sidecar and manifest, `hmac_sha256` on application
  images) before copying. The `firmware/` folder bundled with the flasher
  keeps working offline.
- **URTC-WEB-STUDIO**: `scripts/sync-firmware.mjs` downloads the release
  assets (newest, or a given tag) or reads a local build folder, verifies each
  sha256 and regenerates `src/lib/bundledFirmware.ts`. Nothing in the browser
  code reads GitHub at run time.
- **URTC-UPDATER**: it reads only `urtc.project.json` and the native version
  file of each repository; if it ever downloads firmware it must use the
  release assets described above.
- Anything that links to `.../blob/main/firmware/...` or to a
  `raw.githubusercontent.com/.../firmware/...` URL must use the release asset
  URLs instead. `check_version_consistency.sh` checks the local `firmware/`
  folder (or `build/firmware`) of a built tree and is not part of the CI.

## Bench pre-releases and the "latest" release

GitHub's `releases/latest` endpoint and the `releases/latest/download/` URLs
never return a pre-release. A release whose application images are signed with
the public development key (`signing_key_kind: "dev"`) is therefore published
as a **pre-release** ("bench"): `tools/publish_release_assets.py` refuses it
as an official release, and with `--allow-dev-key` it adds `--prerelease` to
the printed command. Until the first release signed with a private production
key exists, the clients reach the bench images by tag
(`sync_firmware.py --release v0.3.5`, `sync-firmware.mjs --tag <tag>`), and the
bundled copies keep serving offline use.

## Publishing a release

1. `bash build_firmware.sh` (release mode, with `URTC_SIGNING_KEY_FILE` and
   `URTC_RELEASE=1` for a release signed with the private key; see
   `docs/FIRMWARE_SIGNING.TXT`).
2. `python tools/publish_release_assets.py` and check that it ends with
   `RELEASE_ASSETS=PASS` (a development-key set fails; use `--allow-dev-key`
   only for a bench pre-release).
3. Run the printed `gh release create ...` command (write the notes file it
   names first, or replace `--notes-file` with `--notes`).
4. Keep only the newest release's assets; older tags can stay as tags.
