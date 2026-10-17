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

## What each consumer must change

None of these repositories was modified by this change.

### URTC-FLASHER

- `flasher_github.py`: `GITHUB_API_BASE` points at
  `.../contents/firmware`, which is now empty. Switch the listing to
  `.../releases/latest` and build the list from `assets[]` (keep the same
  `name` / `size` / download URL triple, and the same filter: `.bin`,
  `.sha256`, `.manifest.json`; `firmware_manifest.json` is useful too).
- `ALLOWED_DOWNLOAD_HOSTS` is `("raw.githubusercontent.com",)`: add
  `github.com` and `objects.githubusercontent.com`, keep the check both
  before the request and after redirects (`response.geturl()`).
- `tools/sync_firmware.py` and its default source `../URTC/firmware`: a fresh
  checkout of URTC has no such folder. Either build URTC locally (release
  mode fills `../URTC/firmware`), or add a mode that downloads the release
  assets into a temporary folder and then runs the same `check()` (sha256
  against sidecar and manifest, `hmac_sha256` present on application images)
  before copying. The bundled `firmware/` of the flasher keeps working as it
  is.
- `tests/test_github_download.py` uses
  `https://raw.githubusercontent.com/JuanenRac/URTC/main/firmware/` as the
  fixture base and expects the contents-API shape; update it to the release
  assets shape and add the new hosts to the refusal cases.
- `tests/test_firmware_signature.py` runs `sync_firmware.py --check` on the
  bundled folder: unchanged.
- Version check: with firmware 0.3.5 the flasher can read the PATCH number
  over CAN as well; the host contract is in `docs/CANBUS.TXT`
  ("FIRMWARE PATCH NUMBER").

### URTC-WEB-STUDIO

- `scripts/sync-firmware.mjs` (npm script `sync-firmware`) reads
  `../URTC/firmware` by default, expects `firmware_manifest.json` there and
  copies `.bin`, `.hex`, `.elf` and the sidecars into `public/firmware`,
  regenerating `src/lib/bundledFirmware.ts`. Add a second source: download
  `firmware_manifest.json` and the files it names from
  `releases/latest/download/` (or from a given tag) into a temporary folder,
  verify each sha256 against its sidecar, then run the existing copy. Keep the
  local folder as an explicit argument for bench builds.
- The comment in `src/components/FlasherStudio.tsx` that says the binaries are
  byte-for-byte what `URTC/firmware` publishes should say "what the release
  publishes".
- The site build / CI must run the sync (or commit the synced `public/firmware`
  as it does today); nothing in the browser code reads GitHub at run time.

### URTC-UPDATER

- It reads only `urtc.project.json` and the native version file of each
  repository through `raw.githubusercontent.com` (`registry.py`). Both stay in
  git, so nothing has to change. If the updater later downloads firmware, it
  must use the release assets described above and not `firmware/`.

### Other readers

- Anything that links to `.../blob/main/firmware/...` or uses a
  `raw.githubusercontent.com/.../firmware/...` URL for URTC,
  URTC-SMART-RACK or URTC-VISION-TOOL must move to the release asset URLs.
- `check_version_consistency.sh` (URTC) checks the local `firmware/` folder
  (or `build/firmware`) of a built tree; it is not part of the repository CI.

## Publishing a release

1. `bash build_firmware.sh` (release mode, with `URTC_SIGNING_KEY_FILE` and
   `URTC_RELEASE=1` for a release signed with the private key; see
   `docs/FIRMWARE_SIGNING.TXT`).
2. `python tools/publish_release_assets.py` and check that it ends with
   `RELEASE_ASSETS=PASS`.
3. Run the printed `gh release create ...` command (write the notes file it
   names first, or replace `--notes-file` with `--notes`).
4. Keep only the newest release's assets; older tags can stay as tags.
