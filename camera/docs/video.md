# Video on the main camera

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

There is no 60 fps mode.

## Regular video (up to 30 fps)

Regular video comes from the binned mode, so 16:9 video holds at most
2832x1592 of detail: 2560x1440 is the largest size without upscaling.

3840x2160 is not in the backend's size table; the HAL adds it to the main
camera. A 3840x2160 video stream makes the sensor module set
`SENSOR_SPECIAL_MODE_RES_UHD`, which selects the `video_4k_cpp` tuning
for the scaling from the binned mode, as on the stock firmware.

Sizes wider than 3840 cannot be recorded: CAF rejects a JPEG (video
snapshot) stream larger than 4K together with a large video stream.

## Slow motion (constrained high speed)

The backend's fps table and size durations also contain the high speed
rates; the HAL reports only up to 30 fps for regular sessions, so apps
use a constrained high speed session for 120 and 240 fps:

| Size | Rates | Sensor mode | Batch |
|------|-------|-------------|-------|
| 1920x1080 | 120 | res 6 | 4 |
| 1280x720 | 120, 240 | res 5 | 8 at 240 |
| 720x480 | 120, 240 | res 5 | 8 at 240 |

Both modes read a smaller area of the sensor than the binned modes, so
the picture is narrower (looks zoomed in) while recording.

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

120 fps uses Samsung's `hfr_120_3a` tuning without changes.

### Limits

- At 240 fps the H.264 encoder does not keep up all the time; whole
  batches are dropped while the camera waits for encoder buffers.
- Slow motion needs bright light: a 240 fps frame gets at most 1/240 s.
- Open Camera sometimes crashes when starting a high speed session: it
  calls `setRepeatingRequest()` on it, which Android does not allow.
