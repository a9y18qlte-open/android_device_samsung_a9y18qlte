# OSS MDFPP keymaster (a9y18qlte)

Open-source reimplementation of Samsung's `libkeymaster2_mdfpp.so` +
`keystore.mdfpp.so`. It speaks the same `KM_INDATA`/`KM_OUTDATA` protocol to the
same signed `skeymast` TrustZone app (shared `libskeymast_common` codec), so
existing hardware-backed keys keep working. The TrustZone app is unchanged.

Installed as `keystore.mdfpposs.so`, NOT default. Select with
`ro.hardware.keystore=mdfpposs`; fall back with `=mdfpp`.

## Protocol
- `keymaster2_device` ops map to skeymast command ids (1=add_rng_entropy,
  2=generate_key, 3=get_key_characteristics, 8/9/0xa=begin/update/finish,
  0xb=abort, 0xf=configure, 0x10=upgrade_key, 0xe=attest_key).
- Key parameters ride in `par` = `[8]` EXPLICIT `{ SET OF KM_PARAM }`, each
  `SEQUENCE{ INTEGER tag, [0]{INTEGER} | [1]{OCTET} }`, DER-sorted. Responses put
  the param set in `[4]`. Int slot for enum/uint/ulong/date/bool tags, octet for
  bytes/bignum.
- OS version / patchlevel: set once via `configure` (cmd 0xf), computed as
  `release*10000 + minor*100 + sub` and `year*100 + month`. The TA binds them
  internally; it REJECTS client-supplied `OS_VERSION`/`OS_PATCHLEVEL` at
  generate_key, so the ops do not add them to request params.

## Status
Live-tested via standalone harnesses against the real `skeymast` TA (ephemeral
blob-based keys, nothing persisted):
- `add_rng_entropy` -> ok
- `generate_key` (AES) -> ok, key blob + decoded characteristics
- `get_key_characteristics` -> ok
- full op: `generate_key(AES-128/ECB)` -> `begin(ENCRYPT)` -> `update(16B)` ->
  `finish` -> ok, 16-byte ciphertext (operation handle round-trips).

Two things make the stateful ops work (both replicate the stock
nwd_km_KM_INDATA_pack behaviour): generate_key/import inject
KM_TAG_CREATION_DATETIME, and begin injects a per-op timestamp tag (0x60000320,
KM_DATE) — begin returns KM_ERROR_UNKNOWN_ERROR without it. Operation handles
come back as a 9-byte sign-padded INTEGER (64-bit with the top bit set).

Implemented + tested: configure, add_rng_entropy, generate_key,
get_key_characteristics, begin, update, finish, abort. delete_key/all return
UNIMPLEMENTED (as stock — blob keys). NOT yet wired (need the same stock-byte
A/B capture for their exact field use): import_key, export_key, upgrade_key,
attest_key — left NULL so keystore reports them unavailable.

**Test only on a throwaway setup first.** A wrong key-blob format can make
existing keys unusable. Keep stock selectable as fallback.
