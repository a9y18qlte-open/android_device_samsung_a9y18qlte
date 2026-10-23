# OSS MDFPP gatekeeper (a9y18qlte)

Open-source reimplementation of Samsung's `gatekeeper.mdfpp.so`. It speaks the
same protocol to the same signed `skeymast` TrustZone app, so enrolled password
handles and the lockscreen keep working — only the closed userspace glue is
replaced. The TrustZone app itself is unchanged.

## Files
- `gatekeeper_mdfpp.c` — the `gatekeeper_device_t` hw_module (enroll=0x201,
  verify=0x202), named "MDFPP Gatekeeper OSS".
- The DER codec (`km_asn1`), QSEECom transport (`km_transport`) and
  `QSEEComAPI.h` are shared with the keymaster module and live in
  `../common/` (built as `libskeymast_common`). The codec uses BoringSSL
  CBB/CBS (functions, not the version-fragile ASN1_ITEM template structs); build
  against the platform's own BoringSSL headers (soong does) so the CBB struct
  layout matches the linked libcrypto.

## Protocol notes
- Optional KM fields use EXPLICIT context tags (`0xa0|n` wrapping a universal
  INTEGER/OCTET), and present-but-empty octet fields are still emitted.
- Field map: enroll key[4]=current handle, bin0[2]=current pwd, bin1[3]=desired
  pwd; verify key[4]=enrolled handle, bin0[2]=provided pwd, long0[1]=challenge.
- OUTDATA bin0[2] = 58-byte password handle (enroll) / 69-byte auth token
  (verify). err -0x25b/-300 = throttle, long0 = retry ms.
- Links the current `libcrypto` (built from source); the stock blob needed a
  `libcrypto-v33` pin, this does not.

## Status
Live-tested on device via a standalone harness (enroll -> 58-byte handle,
verify(correct) -> 69-byte token, verify(wrong) -> rejected), under a throwaway
uid with nothing persisted, then as the live HAL (lockscreen PIN and
fingerprint). Replaces Samsung's `gatekeeper.mdfpp.so` under the same name
(`ro.hardware.gatekeeper=mdfpp`); the stock blob is no longer extracted.

**Test changes on a throwaway user / disposable /data first** — a wrong handle
format locks the lockscreen and file-based encryption. Keep a known-good build
to flash back.
