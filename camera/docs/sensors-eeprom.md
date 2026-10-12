# Sensor modules and calibration data

## Modules (`/vendor/etc/camera/camera_config.xml`)

| Slot | Sensor | Chromatix | EEPROM / OTP | Actuator | Flash | Position | CSID | Lanes |
|------|--------|-----------|--------------|----------|-------|----------|------|-------|
| 0 | imx576 | imx576_e24qs | dw9808 | dw9808 | pmic | BACK | 0 | 4 |
| 1 | imx576_front | imx576_d24qs | gt24p128c | - | - | FRONT | 1 | 4 |
| 2 | s5k4hayx | s5k4hayx_a08ql | m24c64x | - | - | BACK_AUX | 1 | 4 |
| 3 | s5k3m3 | s5k3m3_c13ql | dw9808_sub | dw9808_sub | pmic | BACK_AUX | 2 | 4 |
| 4 | s5k5e9yx | s5k5e9yx_s05ql | lsi_s5k5e9yx_otp | - | - | BACK_AUX | 2 | 2 |

The stock file mounts the depth sensor at 270 degrees; it is mounted like
the other rear sensors, so the device tree patches it to 90.

Module IDs reported in sysfs (`*_camfw`): main `E24QSL...`, front
`D24QSL...`, ultra-wide `A08QLL...`, tele `C13QLL...`, depth
`S05QLL...`.

## Buses

| EEPROM | Bus | Address | Kernel cell |
|--------|-----|---------|-------------|
| dw9808 (main) | QUP i2c@c176000 | 0x50 | 0 |
| gt24p128c (front) | QUP i2c@c176000 | 0x51 | 1 |
| m24c64x (ultra-wide) | QUP i2c@c176000 | 0x52 | 2 |
| dw9808_sub (tele) | GPIO i2c@27 | 0x53 | 3 |
| S5K5E9 OTP (depth) | CCI | sensor | 4 |

## EEPROM format

The kernel reads each EEPROM at boot into a buffer that mirrors the
EEPROM address space, following the `qcom,memN` blocks in the device
tree: alternating data blocks and 4-byte CRC32 (zlib) blocks. The HAL
gets the buffer through `VIDIOC_MSM_EEPROM_CFG` (`CFG_EEPROM_GET_CAL_DATA`
/ `CFG_EEPROM_READ_CAL_DATA`); `tools/eeprom/eeprom_dump.c` reads it the
same way.

| Offset | Size | Contents |
|--------|------|----------|
| 0x0000 | 252 | header: firmware/module ID at 0x48 (e.g. `E24QSLA00SM`), calibration map version at 0xE0 (`V602V003A9_18QR1`), module serial at 0xAE |
| 0x0100 | 1800 | lens shading: 17x13 rolloff table per R / Gr / Gb / B |
| 0x080C | ~120 | AF: macro (D50 for the tele) at 0x818, infinity at 0x81C, MTF at 0x84E |
| ~0x0886 | 38 | AWB: R/G, B/G, Gr/Gb and golden-module values |
| next | 46-60 | module data |
| 0x08E6 | 2054 | main only: dual-camera calibration (below) |
| 0x1100 / 0x1800 | 1788 / 3324 | PDAF calibration (main; front has an empty 3324-byte block) |

The front EEPROM is shifted: firmware ID at 0x40, map version at 0x70,
module serial at 0x56.

| EEPROM | Size |
|--------|------|
| main (dw9808) | 9472 |
| front (gt24p128c) | 5510 |
| tele (dw9808_sub) | 4096 |
| ultra-wide (m24c64x) | 2308 |
| depth (sensor OTP) | 512 |

Every CRC matches except the last block of the ultra-wide (16 unprogrammed
bytes), the front PDAF block (mostly unprogrammed), the tele's last data
block (mismatch, purpose unknown) and the depth OTP (own checksum scheme).

## Dual-camera calibration

Stored only in the main module's EEPROM, one set per secondary camera:

| | Depth | Tele |
|--|-------|------|
| DLL version | 0x08E6 | 0x0BE6 |
| 512-byte calibration blob | 0x08EA | 0x0BEA |
| tilt X / Y / Z, SX, SY (floats) | 0x0946 | 0x0C46 |

On the test unit the depth tilt is -0.156 / 0.335 / 0.005 with shifts
2.55 / 12.46 and an average error of 0.91; DLL version 20180912. The
kernel exposes the same values in `rear2_tilt`, `rear4_tilt`,
`rear_dualcal` and `rear4_dualcal`.
