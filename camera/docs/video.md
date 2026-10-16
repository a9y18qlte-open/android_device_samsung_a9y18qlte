# Video on the main and front cameras

## Sensor modes

The IMX576 mode table (`libmmcamera_imx576.so`, entries of 0x60 bytes
from 0xac428) has these modes:

| Res | Output | Max fps | Use |
|-----|--------|---------|-----|
| 0 | 2832x2124 | 30 | binned, preview and regular video |
| 1 | 2832x1592 | 30 | binned 16:9 |
| 2 | 2832x1376 | 30 | binned 18.5:9 |
| 3 | 2124x2124 | 30 | binned 1:1 |
| 4 | 5664x4248 | 30 | full resolution, in-sensor remosaic (stills only) |
| 5 | 1280x720 | 240 | high speed, crop of the sensor centre |
| 6 | 1920x1080 | 120 | high speed, crop of the sensor centre |

The stock table has no 60 fps mode. The front camera's
`libmmcamera_imx576_front.so` has the same table at the same offsets.
Stock differs in the high speed modes there: res 5 runs on lower PLL
multipliers (about 221.5 fps) and res 6 is 1280x720; the device tree
patches both to the main camera's (see [Slow motion](#slow-motion-constrained-high-speed)).

## 60 fps (regular sessions)

The device tree patches res 1 (binned 16:9) of both IMX576 libraries
into a 60 fps mode, with the clocking of the high speed modes:

| Register | Stock res 1 | 60 fps res 1 | Note |
|----------|-------------|--------------|------|
| 0x0303 | 4 | 2 | VT clock divider, as res 4-6 |
| 0x0342/3 | 6320 | 4400 | line length: a 2832 px line still fits the MIPI rate |
| 0x0340/1 | 2177 | 3128 | frame length for 60 fps |
| 0x3f80/1 | 0x0230 | 0x03e8 | as res 5/6 |

and its table entry: line/frame length 4400/3128 (with the 826 MHz VT
clock this gives 60.00 fps, measured), op pixel clock 540 MHz, max fps
60. The MIPI rate and PLL multipliers are unchanged; 2832x1592 at 60 fps
is about the pixel rate of stock's 1080p120 mode.

The op pixel clock (table offset 0xc) is what the ISP sizes itself by.
At 826 MHz it splits the sensor over both VFEs, and in that mode the
per-request preview output fails (`MSM_ISP_UPDATE_STREAM` error on VFE1,
then `OUTPUT_ERROR`): display previews drop frames or stall. 540 MHz
covers the real line rate (2832 px per 5.3 us line, about 531 Mpx/s) on
one VFE.

`libmmcamera2_sensor_modules.so` picks a mode by checks per use case; for
the regular video/preview use case (12) no check was enabled, so the
lowest mode (res 0, 30 fps) always won. The device tree enables the fps
check for it: a [60,60] request skips res 0 and gets res 1, 30 fps and
below keep res 0 as before.

The 3A tuning stores its AEC exposure table in sensor lines of the
stock 6320 px line. Res 1's line is now 4400 px, so the tables' fixed
frame rate limit of 2177 lines (1/30 s at 6320) gave at most 11.6 ms per
60 fps frame. Res 1 uses copies of its 3A libraries with the table's
line counts scaled by 6320/4400 (the device tree's
`scale_exposure_table`): `60_preview_3a` and `60_video_3a` on both
cameras. 4K at 30 fps is not affected: the mode picker gives every
session at 30 fps or less res 0.
[tools/chromatix/imx576_e24qs_chromatix-res1-3a.diff](../tools/chromatix/imx576_e24qs_chromatix-res1-3a.diff)
and
[tools/chromatix/imx576_d24qs_chromatix-res1-3a.diff](../tools/chromatix/imx576_d24qs_chromatix-res1-3a.diff)
point res 1 at them; the front one adds a res 1 entry with the front
dimension mask, which its sessions always carry. A 60 fps frame then
gets up to 16.66 ms (the frame stretches to 16.71 ms when it needs all
of it), about 20% brighter pixel values in the dark than at 11.6 ms.

Anti-banding still limits it: the HAL maps AUTO to 60 Hz
(`persist.vendor.camera.set.afd`, default 5) and 16.67 ms does not fit
in a 60 fps frame, so with anti-banding on a 60 fps frame gets 8.33 ms
(10 ms at 50 Hz, `persist.vendor.camera.set.afd=4`). Apps that turn
anti-banding off, or `persist.vendor.camera.set.afd=0`, get the full
frame.

Res 0 is left at 30 fps: as a 60 fps mode, auto exposure halves the
exposure and doubles the gain at 30 fps too. So 60 fps always comes
from the 16:9 mode, and 4:3 sizes at 60 fps are a crop of it.

The HAL offers a fixed [60,60] range on the main and front cameras
(persist.vendor.camera.regular_60fps=0 turns it off) for sizes up to
2560x1440; 2560x1440 at 60 fps is 864000 macroblocks/s, within the H.264
encoder's 972000. Variable ranges stay at most 30 fps, so previews never
switch to the 16:9 mode by themselves. A 60 fps frame gets at most
1/60 s of exposure. 60 fps sessions get 4 more preview/video buffers and
up to 10 requests in flight (CAF's 6 left no slack at 60 fps).

Apps that set [60,60] on the camera but not on the encoder (CameraX,
e.g. Aperture) leave the H.264 encoder configured for 30 fps; the
kernel's msm_vidc then votes clocks, bus and rate control for 30 and the
camera waits for encoder buffers (about 40 fps recorded). The kernel
measures the encoder input rate from buffer timestamps and raises the
session frame rate (lineage-20-ksu, `msm_venc_update_input_fps`).

Previews with the preview capture intent use the preview 3A, whose
table first lengthens the exposure past the frame (lower fps) in the
dark. With a fixed frame rate (Open Camera's video preview, [30,30]) it
stops at about ISO 268 at 30 fps; the video 3A used while recording (and
by apps that preview with the record intent, e.g. CameraX) raises the
gain to ISO 800 at a fixed frame time instead.

## Regular video (up to 30 fps)

Regular video comes from the binned mode, so 16:9 video holds at most
2832x1592 of detail: 2560x1440 is the largest size without upscaling.

3840x2160 is not in the backend's size table; the HAL adds it to the main
and front cameras. On the main camera a 3840x2160 video stream makes the
sensor module set `SENSOR_SPECIAL_MODE_RES_UHD`, which selects the
`video_4k_cpp` tuning for the scaling from the binned mode, as on the
stock firmware. The front camera has no UHD tuning and scales with its
regular video tuning. The device tree's media_profiles have 2160p
camcorder profiles for both cameras, so CameraX apps (Aperture) offer
4K too; media_profiles is read once by zygote, so a changed file needs a
framework restart.

Sizes wider than 3840 cannot be recorded: CAF rejects a JPEG (video
snapshot) stream larger than 4K together with a large video stream.

## Slow motion (constrained high speed)

The backend's fps table and size durations also contain the high speed
rates; the HAL reports only up to 60 fps for regular sessions, so apps
use a constrained high speed session for 120 and 240 fps:

| Size | Rates | Sensor mode | Batch |
|------|-------|-------------|-------|
| 1920x1080 | 120 | res 6 | 4 |
| 1280x720 | 120, 240 | res 5 | 8 at 240 |
| 720x480 | 120, 240 | res 5 | 8 at 240 |

Both modes read a smaller area of the sensor than the binned modes, so
the picture is narrower (looks zoomed in) while recording.

The front camera has the same sizes and rates after two device tree
patches of `libmmcamera_imx576_front.so`:

- Res 6 is 1280x720 at 120 fps in stock, a crop of the same binned
  area the main camera's 1920x1080 res 6 reads. Its output size
  (0x034c-0x034f), crop (0x0408-0x040f) and table entry take the main
  camera's 1920x1080 values; the front's own PLL and frame length still
  give 120 fps. The HAL then reports 1920x1080 at 120 fps for it.
- Res 5 runs its VT and op PLL multipliers at 0x143/0x13c instead of
  0x15d/0x157 and a frame length of 1107: about 221.5 fps. The HAL
  derives the timestamps within a batch from the nominal 240 fps, so
  each batch of 8 ended about 3 ms early and the video stuttered. It now
  uses the main camera's multipliers and frame length 1104 (240 fps);
  the front's res 6 already runs its link at the 0x157 op rate.

In apps that select slow motion as a playback speed for 30 fps playback
(Open Camera's "speed"), the capture rate is 30 fps / speed:

| Speed | Capture rate |
|-------|--------------|
| 0.25x | 120 fps |
| 0.125x | 240 fps |

At 1x with a 120 or 240 fps frame rate the file is recorded at that rate
and can be slowed down later.

### 240 fps tuning

The chromatix XML only has a res 5 entry for the Samsung `SLOW_MOTION`
special mode, which the Samsung camera app sets through its shot mode
parameter. Without it the backend uses `zsl_preview_3a`, whose auto
exposure lengthens the frames in low light (240 fps only at ISO 50).
[tools/chromatix/imx576_e24qs_chromatix-slowmotion.diff](../tools/chromatix/imx576_e24qs_chromatix-slowmotion.diff)
adds a default res 5 entry with `mode_slowmotion_3a` for preview and
video; with it the frame rate stays at 240 and the gain stops at
ISO 800, so the picture gets darker instead. Apply it to
`vendor/etc/camera/imx576_e24qs_chromatix.xml` in the device tree.

The front camera's XML has no res 5 entry and no slow motion 3A at all.
Its sensor module adds front dimension flags to the special mode mask of
every session (`PREVIEW_FRONT_DIM_HIGH,SNAPSHOT_FRONT_DIM_HIGH,VIDEO_FRONT_DIM_HIGH`
at 1280x720), and an entry only matches with that mask, so the generic
front entry with `zsl_preview_3a` won: about 111 fps indoors.
[tools/chromatix/imx576_d24qs_chromatix-slowmotion.diff](../tools/chromatix/imx576_d24qs_chromatix-slowmotion.diff)
adds a res 5 entry with that mask, the front's ISP/CPP tuning and the
main camera's `imx576_e24qs_mode_slowmotion_3a`. Apply it to
`vendor/etc/camera/imx576_d24qs_chromatix.xml` in the device tree.

120 fps uses Samsung's `hfr_120_3a` tuning without changes.

### Limits

- At 240 fps the H.264 encoder does not keep up all the time; whole
  batches are dropped while the camera waits for encoder buffers.
- Slow motion needs bright light: a 240 fps frame gets at most 1/240 s.
- Open Camera sometimes crashes when starting a high speed session: it
  calls `setRepeatingRequest()` on it, which Android does not allow.
