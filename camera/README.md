# QCamera2 HAL for the Samsung Galaxy A9 2018 (a9y18qlte)

An open-source QCamera2 camera HAL (`camera.sdm660`) that runs on top of
Samsung's stock mm-camera backend on the Galaxy A9 2018 (SM-A920F, SDM660).

The HAL source is LineageOS's OSS camera HAL from
[`android_device_asus_sdm660-common`](https://github.com/LineageOS/android_device_asus_sdm660-common)
(`camera/`). Samsung's backend uses its own, incompatible versions of the
HAL <-> backend structures (`cam_capability_t`, `metadata_buffer_t`,
`cam_stream_info_t`, ...), so that HAL crashes when used unchanged. This
repository carries the reverse-engineered Samsung layouts and the HAL
changes needed to run on them.

## Status (LineageOS 22.2, kernel 4.4)

| ID | Camera | Sensor | Resolution |
|----|--------|--------|------------|
| 0 | Rear main | Sony IMX576 | 24 MP (5664x4248), f/1.7 |
| 1 | Front | Sony IMX576 | 24 MP (5664x4248), f/2.0 |
| 2 | Rear ultra-wide | Samsung S5K4HA | 8 MP (3264x2448), f/2.4 |
| 3 | Rear telephoto | Samsung S5K3M3 | 10 MP (3672x2760), f/2.4 |
| 4 | Rear depth | Samsung S5K5E9 | 5 MP (2592x1944), f/1.9 |

Working: preview, full-resolution stills (on the IMX576 cameras, sizes
above 2832x2124 are captured in the sensor's 5664x4248 remosaic mode in
bright scenes; smaller sizes and darker scenes use the binned mode),
video recording, manual ISO / exposure / white balance / focus, RAW,
flash modes, all five cameras as standalone camera IDs.

4K video is scaled from the binned mode with the UHD tuning, like stock.

Not done: logical multi-camera, Samsung-only features (Live Focus, etc.).

## History

- 24 commits from asus sdm660-common `camera/`, up to the commit before
  `sdm660-common: camera: Disable display service support` (that one uses
  an Android 16 libgui API and is not needed here: the display service
  runs on this device). Authors and messages are unchanged.
- Our changes, one topic per commit:
  1. build from any path in its own Soong namespace
  2. `stack/common`: match the Samsung backend ABI
  3. adapt the HAL to that ABI
  4. mm-camera-interface: buffer index overflow in streamon
  5. mm-camera-interface: snapshot stride/scanline padding
  6. HAL3: keep video streams linear
  7. mm-camera-interface: expose the auxiliary cameras

## Integration

The repository builds from any path. Either check it out as its own
project:

```xml
<project path="hardware/samsung/camera-a9y18qlte"
         name="a9y18qlte-open/android_hardware_samsung_camera-a9y18qlte"
         remote="github" revision="lineage-22.2" />
```

or carry a copy in the device tree with `git subtree`. Only list one
copy in a build.

Device tree:

```make
PRODUCT_SOONG_NAMESPACES += \
    hardware/samsung/camera-a9y18qlte

PRODUCT_PACKAGES += \
    camera.sdm660 \
    libmmcamera_interface \
    libmmjpeg_interface \
    libqomx_core
```

- Drop the stock `vendor/lib/hw/camera.sdm660.so`,
  `libmmcamera_interface.so`, `libmmjpeg_interface.so`, `libqomx_core.so`
  and `libhal_dbg.so` from `proprietary-files.txt` (and their
  `blob_fixups`), keep every other camera blob.
- Add the namespace to `namespace_imports` in `extract-files.py`: stock
  blobs such as `libmm-qcamera.so` link against `libmmcamera_interface`.
- Use the standard AIDL camera provider
  (`android.hardware.camera.provider-service_32.lineage`). Samsung's
  `camera3_stream_t` reserved field (`needs_sec_reserved_field`), the
  `extra_ids` list and the `camera.unihal` wrapper only apply to the
  stock Samsung HAL.
- The aux cameras are IDs 2, 3, 4 (the stock HAL used 50, 52, 54);
  update app overlays such as Aperture's `config_ignoredAuxCameraIds`
  (depth sensor = 4). `persist.vendor.camera.expose.aux=0` hides them.

## Documentation

- [docs/architecture.md](docs/architecture.md): camera stack, kernel
  interface, sysfs, device nodes
- [docs/samsung-abi.md](docs/samsung-abi.md): Samsung struct layouts,
  enum values and vendor tags, and how they were recovered
- [docs/sensors-eeprom.md](docs/sensors-eeprom.md): sensor modules, buses,
  EEPROM/OTP calibration format, dual-camera calibration
- [docs/video.md](docs/video.md): sensor modes, 4K, slow motion and the
  240 fps tuning patch
- [docs/reverse-engineering.md](docs/reverse-engineering.md): methods and
  [tools/](tools) to reproduce or extend the work
