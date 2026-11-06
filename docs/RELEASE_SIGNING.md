# Release signing

Every asset of an official URTC release carries a detached Ed25519 signature
made with the key of the releases. It is a layer **in front of** the signature
the bootloader checks on the board: it lets a flasher, a web page or a person
know that a file really comes from the maintainer before anything is sent to a
board.

## The public key

```
ed25519 4020c46d76c8ebacb64c5c7b9f3e7c62475afeb133d7421591988174e0f5dc90
```

The private half lives outside every repository, with an offline copy; it is
read from a file on the machine that signs; GitHub never holds it (the release workflow only
verifies with the public key, from `keys/registry.json`). The same public key is pinned in `tools/release_sign.py`, in the
flasher (`flasher_release_signature.py`) and in the web studio
(`scripts/sync-firmware.mjs`); a test in each compares it with this page.

## What is signed

For every file `<asset>` of a release there is `<asset>.sig`: 128 hexadecimal
digits, the Ed25519 signature of the text

```
urtc-rel/1|<file name>|<sha256 of the file in lowercase hexadecimal>
```

The file name carries the component and the version, so a signed image cannot
be presented as another image or another version. `<asset>.sha256` (the digest
in `sha256sum` format) is published next to it, as before.

## Signing and checking

```
python tools/release_sign.py sign FILE...      # writes FILE.sig (needs the `cryptography` package and the key)
python tools/release_sign.py verify FILE...    # checks FILE.sig against the pinned key (standard library only)
python tools/publish_release_assets.py --sign  # signs the whole set and prints the gh command with the .sig files
```

The key is read from the file named by `URTC_RELEASE_KEY`, or from
`~/.urtc-keys/urtc_release_ed25519.pem`. The tools never print it and refuse
to sign with a key that is not the pinned one.

## The image signature record (`.imgsig`)

Besides the release signature of the assets there is a second signature, made for the bootloaders and not for the people who download:
`<application image>.imgsig`, the 76-byte Ed25519 record (key id, rollback counter, signature) written as exactly 152 hexadecimal digits on one
line. It is made **after** the build and apart from it, from the header that describes the image, the SHA-256 of the image and the counter, so the
build needs no key and the bootloader holds only public keys (`keys/image_trust.json`, `docs/KEYS.md`). The bootloader files carry no record.

The path of an official release, from the build to the published set:

1. The workflow `firmware-build` builds the images on GitHub (input `flavour`: `release` for the official table) and uploads them as an artifact
   with the build manifest and the attestation. It signs nothing and holds no key.
2. On the maintainer's machine the artifact is downloaded and verified (attestation, digests, version, the bootloaders carry exactly the table of
   the chosen flavour), the images and the sidecars are signed with the key of the releases (`.sig`), and for every application image the image
   key adds `<image>.imgsig` with the counter of the release. Both keys stay outside every repository and are never printed.
3. `python tools/publish_release_assets.py --sign --draft` puts the records in the set (the `.imgsig` of each application image and its `.sig`),
   refuses an official set that lacks one or holds a file that is not a record, and prints the `gh release create` command; the workflow
   `release.yml` repeats the check (`tools/verify_release_set.py`) before it publishes. That check needs no key: `tools/verify_image_records.py`
   rebuilds, from the file name and the bytes of each application image, what the bootloader verifies (hardware, version, length, CRC-32, SHA-256,
   counter) and decides as the bootloader would against the release table of `keys/image_trust.json`; it also requires the bootloader of the set to
   hold exactly that table. The header the record covers is never inside the flashed image.
4. The consumers take the record from the release: URTC-FLASHER and URTC-WEB-STUDIO read `<image>.imgsig` next to the image they flash and carry
   it to the bootloader (11 frames of `0x7EC` for the main board, `0x21A` for the slave through the main board). The web studio bundles the
   record of each application image with the image (`scripts/sync-firmware.mjs` downloads and verifies it, and refuses an official set without it
   unless `--allow-unsigned`).

A bootloader that does not find the record installs nothing: an image without its `.imgsig` is refused by the tools before a frame is sent.

## Who checks what

| Consumer | Check |
| --- | --- |
| `tools/publish_release_assets.py` | an official release needs the `.sig` of every asset, and each one must verify; a pre-release may go out without. It also needs the `.imgsig` of every application image (152 hexadecimal digits on one line), which is an asset of its own with its own `.sig`; a bench pre-release may go out without |
| URTC-FLASHER (`tools/sync_firmware.py`, image check before a flash) | verifies the `.sig` of what it downloads or flashes when there is one; refuses a wrong one; an image with none is accepted only while the transition below lasts, and says so |
| URTC-WEB-STUDIO (`scripts/sync-firmware.mjs`) | same, with `node:crypto` |
| The main-board bootloader | the Ed25519 record `<image>.imgsig` of the application image against its table of public keys (see below and `FIRMWARE_SIGNING.TXT`); it holds no secret |
| The expansion-slave bootloader | a plain SHA-256 of the image, and the confirmation of the main board, which verifies the same record (`SLAVE_BOOT_CONFIRMATION.md`) |

## Publishing an official release

An official release has two things a bench pre-release does not: its application images are
built with the **production bootloader keys** (`signing_key_kind: "release"` in their
manifests; a key that is not the public development key), and **every asset has its `.sig`**.
`tools/publish_release_assets.py` refuses to call a set official unless both are true.

The signing is always **local**: both private keys stay on your machine, and GitHub holds none.
The workflow `.github/workflows/release.yml` only **verifies and publishes** what you signed.

**1. On your machine (nothing leaves it yet).**

1. Create the bootloader keys once, outside every repository, and keep an offline copy
   (`docs/FIRMWARE_SIGNING.TXT`, section 3): `~/.urtc-keys/signing_keys.txt` with `master=` and
   `slave=` lines of 64 hexadecimal digits, readable by you only. They are different from the
   Ed25519 key of the releases (`~/.urtc-keys/urtc_release_ed25519.pem`).
2. Raise the versions with `python bump.py` (compiling never does), write the CHANGELOG
   section of the new version, run `URTC_CI=1 py -3 tools/ci_validate.py` and push to `main`.
3. `URTC_SIGNING_KEY_FILE=~/.urtc-keys/signing_keys.txt URTC_RELEASE=1 ./build_firmware.sh`
4. `python tools/publish_release_assets.py --sign --draft` signs every asset (`.sig`), checks the
   set and prints a `gh release create ... --draft` command. Run it: it uploads the signed files
   as an **unpublished draft** of the tag, which nobody else can see. Only one draft per tag.
   (`python tools/verify_release_set.py firmware --tag v<version>` repeats on your machine the
   very check the workflow will make.)

**2. On GitHub.** Actions, "URTC firmware release", Run workflow, branch `main`, the tag
(`v` plus the version of `urtc.project.json`). The job waits for the approval that the `release`
environment requires (required reviewer, deployment branch `main`; set up once under Settings,
Environments). It holds no secret for this and needs none. Then it:

1. checks that the tag is the version of the commit and that no release of it exists;
2. downloads the files of the draft;
3. checks `keys/registry.json` against the embedded copies of the public key, and that the
   public key is the one pinned in `tools/release_sign.py`;
4. runs `tools/verify_release_set.py`: the tag, every digest (`.sha256`, the manifests, the
   index), the versions, that the application images are signed with a production key, the
   `.sig` of **every** asset against the public key, that every `.imgsig` verifies for the image next to it
   under the release trust table (no key is read), and that no file is there that the set does
   not account for;
5. only if all of it passes, creates the release from the files it just verified (not from the
   draft, which could change in between), downloads the published files and verifies them again,
   and removes the draft.

If any check fails nothing is published; the draft stays, so you can fix it (delete the draft,
sign again, upload again) and run the workflow once more. A version that already has a release is
never replaced. The job has `contents: write` because GitHub shows drafts only to a token that can
write; no other permission is granted and no action is used that is not pinned by commit.

**Telling the flasher and the web studio.** After the release is published, the job can send a
`repository_dispatch` event (type `urtc-firmware-released`, with the tag and the commit) to
URTC-FLASHER and URTC-WEB-STUDIO, whose scheduled workflows also start on it. That needs one
repository secret that you create yourself, named `URTC_DISPATCH_TOKEN`: a fine-grained token
that can write to the contents of those two repositories (the permission GitHub asks for to send
the event). Without the secret the step prints a notice and is skipped, and the release is
unaffected. A release made from your own machine does not run the workflow, so it sends nothing.

```
gh secret set URTC_DISPATCH_TOKEN --repo JuanenRac/URTC
```

The flasher and the web studio take their images from the newest official release (GitHub's "latest"):
they keep no file that names another release to use in its place.

## Transition from the development key

Until now the images carry an HMAC made with the public development key, so
nothing proves who published them. The plan, in order:

1. **Until the first official release.** Releases that are signed with the development key are published as
   pre-releases ("bench"). The tools verify a `.sig` when it is there; a download without signatures is
   refused unless you say `--allow-unsigned`. The flasher and the web studio keep no copy of the images:
   they take them from the release; a bench pre-release is reached only by its tag.
2. **First official release.** Built with the production HMAC key and signed
   with `--sign`. From then on the flasher and the web studio treat an asset
   without a valid `.sig` as an error (`REQUIRE_RELEASE_SIGNATURE = True`), and
   the pre-releases of the old scheme are reached only on request.
3. **Later (needs a board).** Ed25519 verification inside the bootloader, so
   the board itself no longer depends on a shared secret. That step is
   tracked with the bootloader work; it needs a free-flash measurement of the
   F303 and a bench.

## Losing or replacing the key

Publish a new public key in a release signed with the old one and in this
page; the apps carry it in a single constant and ship it with their next
version. If the old key is lost, the apps have to be updated by hand once.
