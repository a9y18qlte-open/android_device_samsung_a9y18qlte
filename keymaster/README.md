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

- import_key (raw AES) -> ok; export_key (EC P-256 -> 91-byte X.509 public key)
  -> ok; upgrade_key wired (reaches the TA; returns a keymaster error for a
  fresh key with no upgrade params, as expected).

Implemented + tested: configure, add_rng_entropy, generate_key,
get_key_characteristics, begin, update, finish, abort, import_key, export_key,
upgrade_key. delete_key/all return UNIMPLEMENTED (as stock — blob keys).

NOT wired: attest_key — it injects provisioned attestation-cert params from
secure storage (saved_prov_to_param, tags 0x9000139d..0x900013a4) and assembles
a cert chain; needs its own RE pass. Left NULL so keystore reports it
unavailable.

Field map (int0/long0/key/bin0/bin1/par): import 0x04 int0=format bin0=key
material par=params(+creation datetime) -> OUT bin1=blob par=chars; export 0x05
int0=format key=blob bin0=client_id bin1=app_data -> OUT bin0=exported; upgrade
0x10 int0=1 key=blob par=params -> OUT bin1=upgraded blob.

**Test only on a throwaway setup first.** A wrong key-blob format can make
existing keys unusable. Keep stock selectable as fallback.
