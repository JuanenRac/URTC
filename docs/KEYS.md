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
| `urtc-slave-bootloader-hmac-development` | HMAC-SHA256 | `f6fc5ec3c7c0b8325f088385cd109e637d4558d4535f619cd06f3df2afdc381d` |

### `urtc-release-ed25519`

Signs every asset of an official release (the detached `.sig` files, see [`RELEASE_SIGNING.md`](RELEASE_SIGNING.md)). It is embedded in
`tools/release_sign.py` and in this repository's `docs/RELEASE_SIGNING.md`, in URTC-FLASHER (`flasher_release_signature.py`) and in
URTC-WEB-STUDIO (`scripts/sync-firmware.mjs` and `src/lib/releaseSignature.ts`). The private half lives outside every repository
(the file named by `URTC_RELEASE_KEY`, by default `~/.urtc-keys/urtc_release_ed25519.pem`) and, if the maintainer chooses, in the secret
`URTC_RELEASE_ED25519_PEM` of the protected `release` environment.

### The development keys of the bootloaders

Images built without a production key are signed with the public development key written in the `#else` branch of
`bootloader_crypto.c` (main) and `slaveboot_crypto.c` (slave). They prove nothing about who made an image: they exist so that anybody
can build and test. A release build replaces them with the production keys.

The production HMAC keys of the two bootloaders are secrets. They live in `~/.urtc-keys/signing_keys.txt` (the lines `master=` and
`slave=`) and in the secret `URTC_SIGNING_KEYS` of the release environment, never in a repository. A published image says in its manifest
which kind of key signed it (`signing_key_kind`).

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

## The other ecosystems

HYDRA-UMC and A.R.M.O.R. keep and check their own keys in their own repositories; this registry covers the URTC family only.
