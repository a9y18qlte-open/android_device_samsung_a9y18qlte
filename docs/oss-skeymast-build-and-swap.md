# OSS skeymast HALs — build, test & fallback

The open-source gatekeeper and keymaster modules (under `skeymast/`: `common/`,
`gatekeeper/`, `keymaster/`) replace Samsung's mdfpp blobs under the same names.
They talk to the same signed `skeymast` TrustZone app as stock; only the closed
userspace glue is replaced.

| HAL        | Module / file (`/vendor/lib64/hw/`) | Selected by                     | Replaces |
|------------|--------------------------------------|---------------------------------|----------|
| gatekeeper | `gatekeeper.mdfpp(.so)`              | `ro.hardware.gatekeeper=mdfpp`  | `gatekeeper.mdfpp.so` |
| keymaster  | `keystore.mdfpp(.so)`                | `ro.hardware.keystore=mdfpp`    | `keystore.mdfpp.so`, `libkeymaster2_mdfpp.so`, `libkeymaster_helper_vendor.so` |

`ro.hardware.keystore` must stay `mdfpp`: Samsung's fingerprint HAL
(`libbauthtzcommon`) selects the keymaster TA with
`memcmp(ro.hardware.keystore, "mdfpp", 6)`, so any other value breaks
fingerprint even though keymaster itself works.

## Build just the modules
Use the same env as `build.sh`, or soong rebuilds host tools (70k-step bacon).
```
cd ~/lineage-23
export USE_CCACHE=1 CCACHE_DIR=/home/thongnguyen1809/.ccache CCACHE_EXEC=/usr/bin/ccache
export LINEAGE_BUILD=a9y18qlte TARGET_KERNEL_KSU=false TMPDIR=/dev/shm/tmp
source build/envsetup.sh
source vendor/lineage/vars/aosp_target_release
lunch lineage_a9y18qlte-"$aosp_target_release"-userdebug
m gatekeeper.mdfpp keystore.mdfpp
```
Outputs: `out/target/product/a9y18qlte/vendor/lib64/hw/{gatekeeper,keystore}.mdfpp.so`.

## Test a change without a reboot
- **Module directly:** the dlopen harnesses in `~/gatekeeper-re/oss/`
  (`test_km_so.c`, `km_hmac_test.c`, `km_gcm_test.c`, `km_import_test.c`) load a
  built `.so` from `/data/local/tmp` and drive it like the keymaster@3.0 wrapper.
- **Through keystore2:** swap the `.so` in place, then restart BOTH
  `vendor.keymaster-3-0` and `keystore2` (restarting only the HAL leaves keystore2
  with a dead binder: every call fails `DEAD_OBJECT`, whatever the module), and
  run `pm create-user <name>` — that drives the synthetic-password `importKey`
  that file-based encryption needs. Remove the user afterwards.

## Swap a rebuilt module onto a device
`/vendor` is ext4 and writable on userdebug (`adb root; adb remount`).
```
adb push keystore.mdfpp.so /vendor/lib64/hw/keystore.mdfpp.so
adb shell 'restorecon /vendor/lib64/hw/keystore.mdfpp.so; sync; md5sum /vendor/lib64/hw/keystore.mdfpp.so'
adb reboot
```
Check the md5 before rebooting: unsynced pushes to `/vendor` have come back
zeroed. Keep the known-good `.so` to push back.

If a keymaster change breaks file-based encryption the device boots to recovery
with reason `enablefilecrypto_failed`; `/data` is not wiped. In OrangeFox:
`twrp mount vendor`, push the known-good `keystore.mdfpp.so`, reboot. Going back
to Samsung's blobs means flashing a build that still has them (they also need
`libcrypto-v33`).

## Notes
- keymaster also reads provisioned attestation material from `/efs/DAK`
  (system-readable) for `attest_key`.
- `delete_key`/`delete_all_keys` return UNIMPLEMENTED exactly as stock.
- Both modules link the in-tree BoringSSL; the `libcrypto-v33` pin the stock
  blobs needed is gone.
