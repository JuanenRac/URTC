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

The private half lives outside every repository and outside CI, with an
offline copy. The same public key is pinned in `tools/release_sign.py`, in the
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

## Transition from the development key

Until now the images carry an HMAC made with the public development key, so
nothing proves who published them. The plan, in order:

1. **Now.** Releases that are signed with the development key are published as
   pre-releases ("bench") and may have no `.sig`. The tools verify a `.sig`
   when it is there; a download without signatures is refused unless you say
   `--allow-unsigned`, which is how the existing pre-release is reached:
   `sync_firmware.py --release v0.3.5 --allow-unsigned`.
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
