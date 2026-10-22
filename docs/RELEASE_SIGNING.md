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
read from a file on the machine that publishes, or from the secret of the
protected release job described below. The same public key is pinned in `tools/release_sign.py`, in the
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
There are two ways to make one, with the same result.

**From a machine you control (no secret in GitHub).**

1. Create the bootloader keys once, outside every repository, and keep an offline copy
   (`docs/FIRMWARE_SIGNING.TXT`, section 3): `~/.urtc-keys/signing_keys.txt` with `master=` and
   `slave=` lines of 64 hexadecimal digits, readable by you only. They are different from the
   Ed25519 key of the releases (`~/.urtc-keys/urtc_release_ed25519.pem`).
2. Raise the versions with `python bump.py` (compiling never does) and write the CHANGELOG
   section of the new version.
3. `URTC_SIGNING_KEY_FILE=~/.urtc-keys/signing_keys.txt URTC_RELEASE=1 ./build_firmware.sh`
4. `python tools/publish_release_assets.py --sign` and run the `gh release create` command it
   prints (notes: `python tools/changelog_section.py <version> > RELEASE_NOTES.md`).

**From GitHub (the protected job).** `.github/workflows/release.yml` does steps 3 and 4 and
checks the published files afterwards. It is started by hand (`workflow_dispatch` with the tag,
which must be `v` plus the version of `urtc.project.json`), runs only from `main`, and its job
uses the `release` environment: the secrets are readable only after a reviewer approves the run.
Set it up once (Settings, Environments, `release`: required reviewers, deployment branch `main`)
and give it the two secrets without ever printing them:

```
gh secret set URTC_SIGNING_KEYS         --env release --repo JuanenRac/URTC < ~/.urtc-keys/signing_keys.txt
gh secret set URTC_RELEASE_ED25519_PEM  --env release --repo JuanenRac/URTC < ~/.urtc-keys/urtc_release_ed25519.pem
```

The job writes the keys to files in the runner's temporary folder (mode 600), passes only the
file names to the build, and removes the files at the end whatever happened. A version that
already has a release is never rebuilt. Giving the keys to GitHub is a choice: if the
repository's settings or the account are compromised, so are they; the first way keeps them on
your machine.

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
