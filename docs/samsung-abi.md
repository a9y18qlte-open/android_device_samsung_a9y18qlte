# Samsung backend ABI

All sizes are for 32-bit ARM, where the HAL and backend run. The layouts
in `QCamera2/stack/common/cam_intf.h` and `cam_types.h` mirror Samsung
exactly; `SEC:` comments mark every difference from CAF, and members the
HAL never touches are sized `sec_*` placeholders.

## Summary

| Structure | CAF | Samsung | Verified by |
|-----------|-----|---------|-------------|
| `cam_capability_t` | 21984 | **34984** | 91 field offsets from the stock HAL, live dump of all cameras |
| `metadata_buffer_t` / `parm_buffer_t` | 767200 | **1250720** | 140 member offsets, live per-frame decode |
| `cam_stream_info_t` | 50168 | **50264** | 13 field offsets |

## cam_capability_t

- `MAX_ZOOMS_CNT` 91 -> 800, `MAX_SIZES_CNT` 40 -> 64
- `supported_iso_modes[17]` (17 ISO modes, see enums) - this alone
  accounts for 36 bytes before `zoom_ratio_tbl`
- larger scene (24), AEC (10), white balance (12), focus (12), HFR (10)
  and format (134) arrays
- `cam_padding_info_t` without `usage` (28 bytes)
- `is_quadracfa_insensor` moved after `is_remosaic_lib_present`; at the
  CAF position is the quadra CFA dimension count, which made CAF treat the
  IMX576 as a quad-CFA sensor and cut every picture size to 2832x2124.
  The backend sets it (not `is_quadracfa_sensor`) on the main and front
  IMX576, whose 5664x4248 mode is remosaiced in the sensor
- `sub_fmt[2]`, `IS_TYPE_MAX` 6 (no `IS_TYPE_VENDOR_EIS`)
- no `single_isp_max_size`
- Samsung-only blocks: 1028 bytes after `fps_ranges_tbl`, 1432 bytes
  before `camera_index`

## metadata_buffer_t / parm_buffer_t

`is_valid[CAM_INTF_PARM_MAX]` followed by one member per `CAM_INTF_*` ID.

- IDs 0-233 are the same as CAF. Samsung inserted 10 IDs at 234
  (unnamed), so CAF IDs from 243 are +10 and `CAM_INTF_PARM_MAX` is 270.
  `is_valid` is padded to 272 bytes.
- A 328104-byte Samsung tuning blob is the first member (CAF's
  `CAM_INTF_META_TUNING_PARAMS` moved there with Samsung's own type).
- Size changes: `CAM_INTF_META_AEC_INFO` +1608, `CAM_INTF_PARM_FD` +4,
  `CAM_INTF_PARM_EZTUNE_CMD` +24, `CAM_INTF_PARM_RAW_DIMENSION` is a
  plain dimension (no `opClock`), `CAM_INTF_META_USERZOOM` is 10 bytes.
- `cam_stream_size_info_t` is 592 bytes (64 Samsung bytes before
  `is_secure`).
- `cam_capture_frame_config_t` is 1296 bytes: `MAX_CAPTURE_BATCH_NUM` 32
  with 40-byte entries.
- Samsung-only data: 48 bytes before `CAM_INTF_META_STREAM_INFO`, 5320
  bytes before `CAM_INTF_META_USE_AV_TIMER`, a 413452-byte tail.

## cam_stream_info_t

- `cam_pp_feature_config_t` is 32 bytes larger (after `rotation`), which
  also grows `cam_stream_reproc_config_t` to 536 bytes
- 36 Samsung bytes between `is_type` and `cache_ops`

## Enum values

Read from the stock HAL's `QCameraMap` tables (Android enum <-> backend
enum).

Same as CAF: effects, scene modes 0-22, flash, antibanding, face detect,
test pattern, reference illuminant, aberration, focus 0-8, AEC 0-6.

| Enum | Samsung |
|------|---------|
| ISO | 17 values: AUTO 0, DEBLUR 1, 100 = 5, 200 = 8, 400 = 11, 800 = 14, 1600 = 15, 3200 = 16 (the inserted steps are probably 1/3-stop values, names guessed) |
| HFR | one extra value before 480 fps (= 9) |
| White balance | one extra value before OFF (= 11) |
| AEC metering | + weighted matrix / center / spot (7, 8, 9) |
| Focus | + 3 values (fixed-face = 11) |
| Scene | + 1 (23) |
| `cam_format_t` | CAF up to 71; `Y_ONLY` 107, `NV12_UBWC` 108, `NV21_VENUS` 109, `MAX` 134 (two values inserted between CAF 72 and 104, one after 109) |

## Vendor tags

The stock HAL registers only CAF's `org.codeaurora.qcamera3.*` tags:
sections 0x8000-0x801a, the first 27 of CAF's 33, same order and IDs
(62 tags). No `com.samsung.*` sections. The `stats` section lacks
`is_hdr_scene_values` and `is_hdr_scene_confidence_range`.

## How the layouts were recovered

1. CAF baseline offsets from clang `-fdump-record-layouts` for 32-bit ARM.
2. The stock `camera.sdm660.so` exports all of its C++ symbols, so
   Ghidra decompiles named functions. Anchors:
   - allocation sizes (`initCapabilities` copies 0x88a8 bytes,
     `initParameters` allocates 0x1315a0, `QCamera3Stream::init` 0xc458)
   - static metadata: each `CameraMetadata::update(tag, cap + offset)`
     in `initStaticMetadata` pairs an offset with the CAF field that
     feeds the same tag
   - parameters: every `ADD_SET_PARAM_ENTRY_TO_BATCH` compiles to a store
     at the member offset plus `is_valid[id] = 1`, and every
     `IF_META_AVAILABLE` to an `is_valid[id]` test plus a member access,
     which gives both the Samsung ID and the member offset
3. Edit the CAF headers until every anchor matches (`tools/layout/`
   lists them).
4. Validate on the running stock HAL with Frida: dump
   `qcamera::gCamCapability[]` and decode per-frame metadata in
   `translateFromHalMetadata` (`tools/frida/`).
