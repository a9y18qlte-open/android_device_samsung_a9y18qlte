# Concurrent cameras

Android 11 concurrent camera streaming
(https://source.android.com/docs/core/camera/concurrent-streaming): apps
open two cameras at once, and `CameraManager.getConcurrentCameraIds()`
lists the combinations that support the guaranteed stream table (YUV,
PRIV or JPEG up to s1440p, with a second YUV or PRIV stream up to s720p or
s1440p).

## Limits of the hardware and the backend

- **Two VFEs.** A session takes one. On the IMX576 cameras 0 and 1, a
  PRIVATE stream larger than the active array (2832x2124), like 4K video,
  runs a mode wider than one VFE and the ISP module asks for both
  (`isp_resource_reserve_isp: ... request_dual_isp 1`). JPEG and YUV
  streams of those sizes take the quad-CFA path: they stream in the binned
  mode on one VFE (4K and 5664x4248 YUV, 24 MP JPEG sessions) and take both
  only while a remosaic capture runs the full 5664x4248 mode. The other
  sensors run their full size on one VFE. 1080p60 and 1440p60 on the IMX576
  (res 1) take one.
- **Shared CSI receivers.** The device tree puts two sensors on each of
  CSIPHY/CSID 1 and 2:

  | Slot / camera ID | CSIPHY, CSID |
  |------------------|--------------|
  | 0 main           | 0 |
  | 1 front          | 1 |
  | 2 ultra wide     | 1 |
  | 3 telephoto      | 2 |
  | 4 depth          | 2 |

  A second sensor on a busy PHY fails in `csid_open`
  (`VIDIOC_MSM_CSID_IO_CFG` returns `EINVAL`), so 1 + 2 and 3 + 4 cannot
  stream together. The HAL reports them as conflicting devices.
- **mct refuses the main + front pair.** `mct_pipeline_start_session`
  fails the second of cameras 0 and 1 with "No VFE HW available for another
  pixel-stream" whatever their streams, and the HAL gets `EUSERS`. The
  other pairs start. `persist.camera.mct.multirdi=1` makes mct log
  "Ignoring HW availability errors in multi-rdi mode" and continue; it is
  the only thing that property changes in `libmmcamera2_mct.so`, and no
  other backend library reads it.
- **The ISP module crashes when it runs out of VFEs.** With mct's check
  off, a session that needs both VFEs next to another one crashes the
  provider in `isp_resource_allocate`, in either order.

## What the HAL does

- Resource cost 50 for every camera (half of the camera service's 100), so
  the service allows two sessions, and the conflicting devices above.
- `acquireVfe()` on open: at most two sessions, and none next to a session
  that needs both VFEs. Otherwise the open fails with `EUSERS`, which the
  camera service reports as `ERROR_MAX_CAMERAS_IN_USE`, as the AOSP page
  asks of HALs that cannot run all open cameras at once.
- `admitVfeConfig()` on configure: a configuration with a stream larger
  than the active array that is not on the quad-CFA path, or a high speed or
  vendor session (not known to fit one VFE), fails with `EINVAL` while
  another session is open.
- `reserveVfesForRemosaic()` per still: a remosaic capture takes both VFEs
  while no other session holds one, so no camera opens during it; with
  another camera open the still is captured in the binned mode, upscaled.
- The bokeh depth session opens through the same path and counts too; bokeh
  runs only with streams up to 1920x1440, which take one VFE.

The provider lists every pair of backward compatible cameras whose costs
fit and that do not conflict, and checks concurrent stream combinations
against the guaranteed table. The device sets
`persist.camera.mct.multirdi=1` and declares
`android.hardware.camera.concurrent`.

## Measured (LineageOS 23.2, kernel 4.19)

YUV streams at 640x480 to 1920x1440, mct check off:

| Pair | Result |
|------|--------|
| 0 + 1, 0 + 2, 0 + 3, 0 + 4 | both stream |
| 1 + 3, 1 + 4, 2 + 3, 2 + 4 | both stream |
| 1 + 2, 3 + 4 | second camera fails in `csid_open` |
