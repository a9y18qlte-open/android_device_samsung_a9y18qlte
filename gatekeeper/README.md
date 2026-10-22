# OSS MDFPP gatekeeper (a9y18qlte)

Open-source reimplementation of Samsung's `gatekeeper.mdfpp.so`. It speaks the
same protocol to the same signed `skeymast` TrustZone app, so enrolled password
handles and the lockscreen keep working — only the closed userspace glue is
replaced. The TrustZone app itself is unchanged.

## Files
- `km_asn1.{h,c}` — KM_INDATA/KM_OUTDATA DER codec using BoringSSL CBB/CBS
  (functions, not the version-fragile ASN1_ITEM template structs). Produces
  canonical DER byte-identical to stock. Build against the platform's own
  BoringSSL headers (soong does): the CBB struct layout must match the linked
  libcrypto.
- `km_transport.{h,c}` — QSEECom client: loads `skeymast`, runs the
  pack/send/unpack cycle against it.
- `gatekeeper_mdfpp.c` — the `gatekeeper_device_t` hw_module (enroll=0x201,
  verify=0x202), named "MDFPP Gatekeeper OSS".
- `include/QSEEComAPI.h` — minimal client header (matches the canonical CAF one;
  vendor the full header if preferred).

## Protocol notes
- Optional KM fields use EXPLICIT context tags (`0xa0|n` wrapping a universal
  INTEGER/OCTET), and present-but-empty octet fields are still emitted.
- Field map: enroll key[4]=current handle, bin0[2]=current pwd, bin1[3]=desired
  pwd; verify key[4]=enrolled handle, bin0[2]=provided pwd, long0[1]=challenge.
- OUTDATA bin0[2] = 58-byte password handle (enroll) / 69-byte auth token
  (verify). err -0x25b/-300 = throttle, long0 = retry ms.
- Links the current `libcrypto` (built from source) — do NOT apply the
  `libcrypto-v33` replace_needed fixup used for the prebuilt mdfpp blobs.

## Status
Live-tested on device via a standalone harness (enroll -> 58-byte handle,
verify(correct) -> 69-byte token, verify(wrong) -> rejected), under a throwaway
uid with nothing persisted. Installed as `gatekeeper.mdfpposs.so`; NOT the
default. Select with `ro.hardware.gatekeeper=mdfpposs`, fall back with `=mdfpp`.

**Test only on a throwaway user / disposable /data first** — a wrong handle
format locks the lockscreen and file-based encryption. Keep stock as fallback.
