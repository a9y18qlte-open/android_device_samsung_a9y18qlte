# Reverse-engineering notes

## Tools used

- Ghidra (headless) on the stock `camera.sdm660.so`,
  `libmmcamera_interface.so`, `libmmcamera2_mct_shimlayer.so` and
  `libmmcamera2_mct.so`. Use the libraries from the phone: the device
  tree binary-patches some of them during extraction.
- clang `--target=armv7a-linux-androideabi -fdump-record-layouts` for
  offsets of a header variant. Compile as C++ so anonymous typedef'd
  structs are named in the dump.
- Frida (`frida-inject`, 32-bit, on the device; needs SELinux permissive
  while injecting) against the running provider.
- `strace -f -p <provider>` during camera open for the device nodes and
  ioctls used.

## Workflow

1. Find a field use in the decompiled stock HAL (preferably a whole
   anchor: an allocation size, a metadata tag update, a parameter batch
   entry).
2. Record the offset and the CAF field it corresponds to (see
   `tools/layout/anchors_*.txt`).
3. Change the header, rebuild the clang layout, compare all anchors.
   Regions without anchors can hide shifts: two fields in this HAL were
   found only once the OSS HAL ran (`max_pixel_bandwidth` and the quadra
   CFA fields), so check new results on the device.
4. Validate with live data.

## tools/

| Path | Use |
|------|-----|
| `tools/layout/anchors_cap.txt`, `anchors_meta.txt`, `anchors_stream.txt` | Samsung offsets with their source function |
| `tools/frida/dump_cap.js` | print `gCamCapability[0..5]` as hex (inject into the stock provider) |
| `tools/frida/meta_live.js` | decode frame number, exposure, ISO, crop for the first frames |
| `tools/eeprom/eeprom_dump.c` | read the kernel's cached EEPROM/OTP buffer (read-only) |
| `tools/eeprom/decode_eeprom.py` | check CRCs and decode headers, AF and dual calibration |
| `tools/chromatix/imx576_e24qs_chromatix-slowmotion.diff` | 240 fps tuning entry for the device tree (see [video.md](video.md)) |

`eeprom_dump.c` is freestanding (raw syscalls, no libc). Build it in ARM
mode against the kernel uapi headers:

```
clang --target=armv7a-linux-gnueabi -marm -O2 -static -nostdlib \
    -ffreestanding -fno-stack-protector -fuse-ld=lld \
    -I <kernel>/include/uapi -I <bionic>/libc/kernel/uapi \
    -I <bionic>/libc/kernel/uapi/asm-arm -I <bionic>/libc/include \
    -o eeprom_dump eeprom_dump.c
adb shell /data/local/tmp/eeprom_dump /dev/v4l-subdev12 > main.bin
```

Frida, with the stock HAL running:

```
adb shell 'setenforce 0; /data/local/tmp/frida-inject32 \
    -p $(pidof android.hardware.camera.provider-service_32.samsung) \
    -s /data/local/tmp/dump_cap.js -e; setenforce 1'
```

## Open questions

- names and contents of the Samsung-only blocks and the 10 new
  `CAM_INTF_*` IDs
- exact positions of the inserted `cam_format_t` values
- Samsung's dual-camera / Live Focus path
