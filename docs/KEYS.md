# Keys of the URTC family

Every key that the URTC programs and bootloaders rely on, in one place: what it is for, its public value, where that value is
embedded and where the private half lives. The machine-readable form is [`keys/registry.json`](../keys/registry.json); this page
and the registry are checked against each other and against the code by `tools/keys.py`.

**No secret is written in any repository.** A public value below is public on purpose; a private half is only ever named by the
place it lives in.

## The keys

| Id | Algorithm | Public value |
|---|---|---|
| `urtc-release-ed25519` | Ed25519 | `4020c46d76c8ebacb64c5c7b9f3e7c62475afeb133d7421591988174e0f5dc90` |
| `urtc-main-bootloader-hmac-development` | HMAC-SHA256 | `2526cc3e9011ec7c49ecd2d2b7f389e359aa246014ff5154a0af8a3c3bf95581` |

### `urtc-release-ed25519`

Signs every asset of an official release (the detached `.sig` files, see [`RELEASE_SIGNING.md`](RELEASE_SIGNING.md)). It is embedded in
`tools/release_sign.py` and in this repository's `docs/RELEASE_SIGNING.md`, in URTC-FLASHER (`flasher_release_signature.py`) and in
URTC-WEB-STUDIO (`scripts/sync-firmware.mjs` and `src/lib/releaseSignature.ts`). The private half lives outside every repository
(the file named by `URTC_RELEASE_KEY`, by default `~/.urtc-keys/urtc_release_ed25519.pem`); the signing is done on the maintainer's machine
and the release workflow only checks the signatures with the public key.

### The development keys of the bootloaders

The main bootloader holds no secret and no HMAC key: it trusts the public Ed25519 keys of its trust table (`keys/image_trust.json`, see
[`FIRMWARE_SIGNING.TXT`](FIRMWARE_SIGNING.TXT)). The bench table holds a public development key (its seed is published with the shared
reference), so a bootloader built with it proves nothing about who made an image; the official table is empty until the maintainer creates
the image-signing key, which is not the key that signs release assets. Its private half stays outside every repository.

The HMAC key in the table above is the public development key of the main bootloader of the earlier generation. It lives in `firmware_signing.py`
(only to keep producing the HMAC those bootloaders check). It proves nothing about who made an image: it exists so that anybody can build and test.
The expansion slave bootloader holds no key of any kind: its update path installs an image only when the main board confirms it.

The production HMAC key for those bench bootloaders is a secret. It lives in `~/.urtc-keys/signing_keys.txt` (the line `master=`; an old `slave=` line is ignored) on the maintainer's
machine, never in a repository or in GitHub. A published image says in its manifest which kind of key signed it (`signing_key_kind`).

## Checking

```
python tools/keys.py list                 # the keys and where each one is embedded
python tools/keys.py verify               # every embedded public key equals the registered one
python tools/keys.py verify --strict      # also fail when a repository is not checked out next to this one
```

`verify` reads the files named in the registry, finds the key with the pattern given there (or the development array of a
bootloader) and compares it with the registered value. A program that carries a different key, a file that no longer has a key and (with
`--strict`) a missing repository are failures. The CI runs it; the repositories that are not checked out beside it are reported as
skipped, so run it with `--strict` on a machine that has all of them before a release.

## Changing a key

1. Publish the new public key in a release signed with the old one, and write it in `keys/registry.json` and in this page.
2. Change the single constant in each program that embeds it; `tools/keys.py verify --strict` lists the ones that still carry the old value.
3. Ship the programs with their next versions. If the old private half is lost, the programs have to be updated by hand once.

## The image-signing keys (public trust table)

The keys that authenticate an application image on the bus are a different set from the release key above and are listed in `keys/image_trust.json`, not in the registry: a `dev` table (a public development key, bench builds only) and a `release` table that stays empty until the maintainer provisions the image-signing key. Only public keys are written there. `python tools/image_trust.py validate` checks the file, `python tools/image_trust.py check BIN master|slave dev|release` checks that a compiled bootloader holds exactly that table at its fixed offset, and `python tools/image_trust.py fingerprints release` prints the key ids the local release tool pins before it signs anything.

## The other ecosystems

HYDRA-UMC and A.R.M.O.R. keep and check their own keys in their own repositories; this registry covers the URTC family only.
