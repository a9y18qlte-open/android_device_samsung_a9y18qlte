# OSS skeymast HALs — VM build & on-device swap

Covers building and switching to the open-source gatekeeper/keymaster modules
(under `skeymast/`: `common/`, `gatekeeper/`, `keymaster/`) on branch
`oss-gatekeeper-test`.
They talk to the same signed `skeymast` TrustZone app as stock; only the closed
userspace glue changes. Both are proven via standalone harnesses
(`~/gatekeeper-re/oss/`); this is the integration path.

## 0. Risk summary (read first)
- **gatekeeper** backs the lockscreen; **keymaster** backs keystore2 + file-based
  encryption. A broken keymaster can bootloop or make keys/`/data` unreadable.
- Switch **gatekeeper first**, verify for a day, then keymaster separately.
- Always keep a known-good build (`~/lineage-23.2-20261020-...a9y18qlte.zip`) and
  OrangeFox ready to reflash. The swap is one prop; the fallback is one prop.

## 1. Get the branch onto the VM (without replacing the VM tree)
`push_to_vm.sh` only creates/updates the `oss-gatekeeper-test` *ref* on the VM;
it does NOT change the VM's checked-out build branch or working files, and git
refuses to overwrite the VM's active branch. The replacement risk is in a full
`git checkout` of our branch — our branch is a complete tree cut from a local
branch, so switching to it could diverge from any VM-only commits/edits.

So do a **surgical apply**: push the ref, then check out only our additions onto
whatever the VM already has (no branch switch):
```
~/vm.sh start
~/all_build_scripts/push_to_vm.sh ~/android_device_samsung_a9y18qlte   # creates ref oss-gatekeeper-test
~/vm.sh ssh << 'EOF'
set -e
cd ~/device_samsung_a9y18qlte
git stash list; git status --short | head       # note any VM-only changes first
git checkout oss-gatekeeper-test -- skeymast
# device.mk: just APPEND the packages (PRODUCT_PACKAGES += is additive, so this
# never clobbers VM-only edits):
grep -q 'keystore.mdfpposs' device.mk || \
  printf '\nPRODUCT_PACKAGES += \\\n    gatekeeper.mdfpposs \\\n    keystore.mdfpposs\n' >> device.mk
git --no-pager diff --stat
EOF
```
This leaves the VM's build branch and every other file exactly as-is; only
`skeymast/` and two `device.mk` lines are added. (If you instead want the VM fully on our branch and know it matches, a
plain `git checkout oss-gatekeeper-test` works — but verify no VM-only changes
with `git status` first.) The tree is bind-mounted into
`~/lineage-23/device/samsung/a9y18qlte`.

To undo the surgical apply: drop the appended `PRODUCT_PACKAGES` block from
`device.mk` and `rm -rf skeymast`.

## 2. Build just the two modules (fast)
Use the SAME env as `build.sh` or soong rebuilds host tools (70k-step bacon).
```
~/vm.sh ssh << 'EOF'
set -e
cd ~/lineage-23
export CCACHE_DIR=/home/thongnguyen1809/.ccache CCACHE_EXEC=/usr/bin/ccache
export LINEAGE_BUILD=a9y18qlte TARGET_KERNEL_KSU=false
source build/envsetup.sh
source vendor/lineage/vars/aosp_target_release
lunch lineage_a9y18qlte-"$aosp_target_release"-userdebug
m gatekeeper.mdfpposs keystore.mdfpposs libskeymast_common
EOF
```
Outputs (vendor modules):
`out/target/product/a9y18qlte/vendor/lib64/hw/{gatekeeper.mdfpposs,keystore.mdfpposs}.so`

Pull them:
```
~/download_build_from_vm.sh out/target/product/a9y18qlte/vendor/lib64/hw/gatekeeper.mdfpposs.so
~/download_build_from_vm.sh out/target/product/a9y18qlte/vendor/lib64/hw/keystore.mdfpposs.so
~/vm.sh stop            # free credit
```

## 3. Safety gate — A/B compare (no prop change, no reboot risk)
Push the built `.so`s next to stock and run the standalone harnesses, which
exercise the real module ops against the TA without touching gatekeeperd /
keystore2:
```
adb root; adb remount                      # /vendor is ext4, writable on userdebug
adb push gatekeeper.mdfpposs.so /vendor/lib64/hw/
adb push keystore.mdfpposs.so  /vendor/lib64/hw/
# harnesses from ~/gatekeeper-re/oss (built per that dir's notes):
adb shell LD_LIBRARY_PATH=/vendor/lib64:/system/lib64 /data/local/tmp/gk_test     # enroll/verify
adb shell LD_LIBRARY_PATH=/vendor/lib64:/system/lib64 /data/local/tmp/km_mod_test # generate/characteristics
adb shell LD_LIBRARY_PATH=/vendor/lib64:/system/lib64 /data/local/tmp/km_op       # begin/update/finish
```
Proceed only if all pass (enroll=58 / verify=69 / generate blob + chars /
AES ciphertext). This is the go/no-go gate.

## 4. Switch gatekeeper (reboot) — do this one alone first
`ro.hardware.*` are read-only, set at boot, so the switch is a prop edit + reboot.
```
adb root; adb remount
# flip ONLY gatekeeper; leave keystore on stock for now
adb shell 'sed -i "s/^ro.hardware.gatekeeper=mdfpp$/ro.hardware.gatekeeper=mdfpposs/" /vendor/build.prop'
sync; adb reboot
```
Verify after boot: device boots, lockscreen PIN/password unlocks, fingerprint
works, `adb shell lshal | grep gatekeeper` shows the service up, no gatekeeperd
crashes in `logcat -b crash`. Live for a day.

Fallback (instant): set it back to `=mdfpp` and reboot. If it won't boot, boot
OrangeFox, mount vendor, edit the prop back (or reflash the known-good zip).

## 5. Switch keymaster (separate reboot, after gatekeeper is trusted)
```
adb root; adb remount
adb shell 'sed -i "s/^ro.hardware.keystore=mdfpp$/ro.hardware.keystore=mdfpposs/" /vendor/build.prop'
sync; adb reboot
```
Verify: boots to home (FBE unlock ok), existing keys still work (e.g. a banking/
auth app, screen-lock credential), `adb shell cmd keystore ...` or Settings
security, no keystore2 crashes. attest_key only matters for apps using key
attestation / Play Integrity hardware.

Fallback: set `ro.hardware.keystore=mdfpp` and reboot.

## 6. Making it the default (only after both are trusted)
Change `vendor.prop` in the tree:
```
ro.hardware.gatekeeper=mdfpposs
ro.hardware.keystore=mdfpposs
```
and do a normal `./build.sh`. Until then the modules ship but stay unselected,
so a normal build is unaffected.

## Notes
- The modules read the TA app `skeymast` from `/vendor/firmware_mnt/image`;
  keymaster also reads provisioned attestation material from `/efs/DAK` (present,
  system-readable) for attest_key.
- delete_key/delete_all_keys return UNIMPLEMENTED exactly as stock (blob keys).
- Do NOT add the `libcrypto-v33` blob_fixup to these modules — they build from
  source against the platform BoringSSL (see each module's README).
