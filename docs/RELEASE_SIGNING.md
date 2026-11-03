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

## Who checks what

| Consumer | Check |
| --- | --- |
| `tools/publish_release_assets.py` | an official release needs the `.sig` of every asset, and each one must verify; a pre-release may go out without |
| URTC-FLASHER (`tools/sync_firmware.py`, image check before a flash) | verifies the `.sig` of what it downloads or flashes when there is one; refuses a wrong one; an image with none is accepted only while the transition below lasts, and says so |
| URTC-WEB-STUDIO (`scripts/sync-firmware.mjs`) | same, with `node:crypto` |
| The bootloader on the board | still the HMAC-SHA256 of the image (see `FIRMWARE_SIGNING.TXT`) |

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
   `.sig` of **every** asset against the public key, and that no file is there that the set does
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

After the first official release exists, the flasher and the web studio take their images from
it: delete `firmware_release.json` (flasher) and `firmware-release.json` (web studio), which only
name the bench pre-release to use while there is no official one.

## Transition from the development key

Until now the images carry an HMAC made with the public development key, so
nothing proves who published them. The plan, in order:

1. **Until the first official release.** Releases that are signed with the development key are published as
   pre-releases ("bench"). The tools verify a `.sig` when it is there; a download without signatures is
   refused unless you say `--allow-unsigned`. The flasher and the web studio keep no copy of the images:
   they take them from the release, and while GitHub's "latest" answers 404 (it ignores pre-releases) they use
   the bench release named in `firmware_release.json` / `firmware-release.json`.
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
