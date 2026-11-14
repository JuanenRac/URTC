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
  `URTC_CI`) write the images to `firmware/` locally. In CI mode
  (`URTC_CI=1`) they write to `build/firmware` and touch no tracked file.
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

Every asset of an official release also has a `<asset>.sig` (Ed25519, see
`docs/RELEASE_SIGNING.md`).

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
  images, the release signature) before copying. The `firmware/` folder is
  not in git: the window and the sync tool fill it from the newest official
  release (GitHub's "latest").
- **URTC-WEB-STUDIO**: `scripts/sync-firmware.mjs` downloads the release
  assets (newest, or a given tag) or reads a local build folder, verifies each
  sha256 and the release signature and regenerates `src/lib/bundledFirmware.ts`.
  `public/firmware/` is not in git: the CI and the Pages build run the sync
  (the newest official release). Nothing in the browser code reads GitHub at run time.
- **URTC-UPDATER**: it reads only `urtc.project.json` and the native version
  file of each repository; if it ever downloads firmware it must use the
  release assets described above.
- Anything that links to `.../blob/main/firmware/...` or to a
  `raw.githubusercontent.com/.../firmware/...` URL must use the release asset
  URLs instead. `check_version_consistency.sh` checks the local `firmware/`
  folder (or `build/firmware`) of a built tree; the CI runs it after the firmware compile.

## Bench pre-releases and the "latest" release

GitHub's `releases/latest` endpoint and the `releases/latest/download/` URLs
never return a pre-release. A release whose application images are signed with
the public development key (`signing_key_kind: "dev"`) is therefore published
as a **pre-release** ("bench"): `tools/publish_release_assets.py` refuses it
as an official release, and with `--allow-dev-key` it adds `--prerelease` to
the printed command. Until the first release signed with a private production
key exists, the clients reach the bench images by tag
(`sync_firmware.py --release v0.3.8`, `sync-firmware.mjs --tag <tag>`); they keep no copy of their own.

## The flasher and the web studio keep no copy of the images

Neither repository carries the binaries any more. A person who needs them offline runs
`python tools/sync_firmware.py` (flasher) or `npm run sync-firmware` (web studio) once while connected;
after that the files are in the ignored `firmware/` folder. To use a local build instead of a release:

```
cd URTC
python tools/publish_release_assets.py --allow-dev-key --sign     # signs every asset and checks the set
python ../URTC-FLASHER/tools/sync_firmware.py firmware --allow-unsigned
cd ../URTC-WEB-STUDIO
node scripts/sync-firmware.mjs ../URTC/firmware --allow-unsigned
```

Leave out `--allow-dev-key` once the images are signed with the production key.

## Compile in Actions, sign locally

The official images are compiled by GitHub Actions and signed only on the maintainer's machine; the private keys never leave it.

1. Raise the versions (`python bump.py`), write the CHANGELOG section, run `URTC_CI=1 py -3 tools/ci_validate.py` and merge to `main`.
2. Actions, "URTC firmware build (release candidate)", Run workflow, branch `main`, the version of `urtc.project.json`. It holds no secret. It compiles
   the four components (main application, main bootloader, expansion-slave application and bootloader) with `URTC_RELEASE=1` (the official trust table of
   `keys/image_trust.json`), a build date taken from the commit (`SOURCE_DATE_EPOCH`) and one pinned release of the Arm GNU toolchain (checked by its SHA-256),
   on two separate runners. It compares every file of the two builds and keeps the artifact `firmware-rc-urtc-<version>` for seven days only when they are the
   same bytes. Inside it checks that the version inside each application image (the header slot) is the one of its name and the manifest
   (`tools/check_image_versions.py`) and that the main bootloader holds exactly the official trust table.
3. On the maintainer's machine the artifact of the last successful run is downloaded (`gh run download`) into an empty folder and verified before anything is
   signed: `gh attestation verify` for every file, `sha256sum -c SHA256SUMS`, the build manifest (`reproducible: true`, `signing_key_kind: "release"`, the version
   and the commit of the repository), and the trust table that the main bootloader holds. Then the maintainer's signing tool adds, with the keys of
   `~/.urtc-keys` (never printed, never in a repository), the `<asset>.sig` of every file of the four components and the `<image>.imgsig` of each application
   image, and runs `tools/verify_release_set.py` on the result before anything is uploaded.
4. In that folder: `python tools/publish_release_assets.py --firmware-dir <folder> --sign --draft`, check that it ends with `RELEASE_ASSETS=PASS`, run the
   `gh release create ... --draft` command it prints, then `python tools/verify_release_set.py <folder> --tag v<version>`.
5. Actions, "URTC firmware release", Run workflow, branch `main`, the tag: it verifies the draft against the public keys and publishes it.

What is reproducible: for one commit the images (`.bin`, `.hex`) and their `.sha256` sidecars are the same bytes in every run, on any runner of the same
image, and across commits that do not change the sources. The manifests (`*.manifest.json`, `firmware_manifest.json`) carry the build date, which is the date
of the commit, so they change with the commit and nothing else. Nothing else varies: no clock, no random value and no path of the runner is inside an
image (the workflow fails when it finds one). What could change the images is another compiler: the toolchain release is pinned, so an image compiled
locally with another one is not byte-equal to the one from Actions; the Actions build is the one that is signed. Linux and Windows builds of the same
commit (`build_firmware.sh`, `build_firmware.bat`) are compared byte by byte by the manual workflow "Build scripts compared".

## Publishing a release from a local build

Kept for a build made on the maintainer's machine (for example with a different toolchain):

0. For an official release steps 1 to 3 are done on the maintainer's machine, with `--draft` in step 2 so that the
   signed files are uploaded unpublished; the job `.github/workflows/release.yml` then only verifies them against the
   public key and publishes (`docs/RELEASE_SIGNING.md`, "Publishing an official release").
1. `bash build_firmware.sh` (release mode, with `URTC_SIGNING_KEY_FILE` and
   `URTC_RELEASE=1` for a release signed with the private key; see
   `docs/FIRMWARE_SIGNING.TXT`).
2. `python tools/publish_release_assets.py --sign` and check that it ends with
   `RELEASE_ASSETS=PASS` (a development-key set fails; use `--allow-dev-key`
   only for a bench pre-release). `--sign` writes the `<asset>.sig` of every
   asset with the key of the releases; see `docs/RELEASE_SIGNING.md`.
3. Run the printed `gh release create ...` command (write the notes file it
   names first, or replace `--notes-file` with `--notes`).
4. Keep only the newest release's assets; older tags can stay as tags.
