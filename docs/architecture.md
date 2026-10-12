# Camera stack on the Galaxy A9 2018

## Overview

```
Camera2 apps
  └─ cameraserver
      └─ AIDL camera provider (android.hardware.camera.provider-service_32.*)
          └─ camera.sdm660.so          HAL3 (this repository, or stock Samsung)
              └─ libmmcamera_interface  (this repository, or stock)
                  ║ in-process calls through cam_shim_packet_t
                  └─ libmmcamera2_mct_shimlayer.so      stock Samsung
                      └─ libmmcamera2_mct / isp / sensor / stats / pproc /
                         iface / imglib, chromatix_* tuning libraries
                          ║ V4L2 + msm camera uapi ioctls, sysfs
                          └─ kernel camera_v2 driver (msm-4.4)
```

There is no `mm-qcamera-daemon`: its init service is commented out and
the whole mm-camera backend (about 256 libraries including the chromatix
tuning sets) is loaded into the camera provider process. The HAL talks to
it in-process: `mm_camera_interface` hands `cam_shim_packet_t` commands to
`mct_shimlayer_process_event()` and shares the parameter / metadata
buffers. The shim packet, bundle, buffer-registration and buffer-mapping
structures are the same as in CAF; the incompatible part is the content of
the shared buffers (see [samsung-abi.md](samsung-abi.md)).

## Kernel interface

The kernel is CAF `LA.UM.8.2.r1-05100-sdm660.0` plus Samsung's changes
(one import commit in the a9y18qlte-open kernel). In `include/uapi/media`
Samsung changed:

- `msm_eeprom_cfg_data.is_supported` u8 -> u16, `eeprom_read_t` /
  `eeprom_write_t` gained address and size fields, new
  `CFG_EEPROM_*` commands inserted before `CFG_EEPROM_GET_MM_INFO`
- `CFG_MATCH_ID`, `CFG_ACTUATOR_SLEEP/ACTIVE`, `CFG_SINIT_HWB`,
  `CFG_FLASH_TORCH` and IR flash commands, `CAMERA_4`,
  `MSM_CAMERA_I2C_VARIABLE_LENGTH_DATA` (inserted, renumbers the masks),
  `FLASH_DRIVER_EXT_PMIC`, `ACTUATOR_HALL_EFFECT`, `MSM_CAMERA_SUBDEV_OTP`
- `MSM_MAX_CAMERA_SENSORS` 6 -> 5 (also the HAL's
  `MM_CAMERA_MAX_NUM_SENSORS`), `MSM_CAMERA_MAX_STREAM_BUF` 72 -> 128
- `CAM_INFO_*` enums for the camera sysfs

Sensor media entities carry the position in `entity.flags` bits 16-24
(front = bit 16, aux = `AUX_CAMERA_B` = bit 24), filled from
`camera_config.xml` `<Position>` through the sensor slave info.

### V4L2 nodes

| Node | Entity |
|------|--------|
| v4l-subdev0-2 | msm_csiphy |
| v4l-subdev3-6 | msm_csid |
| v4l-subdev7-9 | actuators |
| v4l-subdev10 | msm_otp (S5K5E9 sensor OTP) |
| v4l-subdev11-14 | msm_eeprom (tele, main, front, ultra-wide) |
| v4l-subdev15 | msm_camera_flash |
| v4l-subdev16 | msm_sensor_init |
| v4l-subdev17 | cpp |
| v4l-subdev18-19 | vfe |
| v4l-subdev20 | msm_ispif |
| v4l-subdev21 | msm_buf_mngr |
| v4l-subdev22-26 | imx576, imx576_front, s5k4hayx, s5k3m3, s5k5e9yx |
| video0 / video1 / video3-7 | msm-config / msm_jpegdma / msm-sensor |

### /sys/class/camera

The kernel creates `rear/`, `front/` and `flash/` (Samsung
`CAMERA_SYSFS_V2`). At provider start `libmmcamera2_sensor_modules`
writes the EEPROM firmware information to `*_camfw`, `*_camfw_full`,
`*_checkfw_*`, `rear_calcheck`, `rear_fwcheck` and `rear_camfw_load`,
and the HAL reads `rear_camfw` / `front_camfw` back. The nodes are
`system:system 0664`; Samsung's provider ran in group `system`, the AOSP
provider does not, so the writes fail with EACCES unless the nodes are
`chown system camera` (as `rear_flash` already is). `rear_flash` is the
torch control (SM5705 LED driver).

Strings in the stock HAL also reference `rear_moduleinfo`,
`front_moduleinfo`, `ssrm_camera_info` and `ois/ois_exif`, which this
kernel does not create; the HAL tolerates their absence.

## Fixes this HAL needs that the stock HAL had as binary patches

| Problem | Fix |
|---------|-----|
| HAL3 preview never starts: `mm_stream_streamon()` walks 128 buffer slots with an `int8_t` | unsigned index |
| SMMU fault on snapshots with odd height/2 (e.g. 5664x3186) or unaligned stride (3672-wide tele) | pad scanline to 4, stride to 32 |
| Green video: the stock encoder only takes linear NV12 | video stays NV12_VENUS, preview UBWC |

Still applied to stock blobs in the device tree: the ISO calculation in
`libmmcamera2_sensor_modules.so` (Samsung never fills the digital gain)
and the depth sensor mount angle in `camera_config.xml`.
