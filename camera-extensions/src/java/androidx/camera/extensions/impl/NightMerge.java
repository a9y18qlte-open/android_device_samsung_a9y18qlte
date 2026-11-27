/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package androidx.camera.extensions.impl;

import android.util.Log;

import java.util.ArrayList;
import java.util.List;
import java.util.stream.IntStream;

/**
 * Merges a burst of equally exposed YUV frames: the sharpest frame is the reference and frames
 * much softer than it (focus still moving, shake) are left out; each frame is aligned to the
 * reference by a global translation ({@link HdrFusion#align}), every pixel is averaged with weights that drop
 * where it differs from the reference (moving objects), and the result is brightened with a
 * gain picked from its median, through a curve that keeps the highlights.
 */
final class NightMerge {
    private static final String TAG = "NightMerge";
    private static final float GHOST_SIGMA = 16f;
    private static final float TARGET_MEDIAN = 64f;
    private static final float MAX_GAIN = 4f;
    private static final float KEEP_SHARPNESS = 0.8f;

    private static final int[] WEIGHT = new int[256];

    static {
        for (int d = 0; d < 256; d++) {
            WEIGHT[d] = Math.round(256f * (float) Math.exp(-d * d / (2 * GHOST_SIGMA * GHOST_SIGMA)));
        }
    }

    private NightMerge() {
    }

    static HdrFusion.Frame merge(HdrFusion.Frame[] burst) {
        float[] sharpness = new float[burst.length];
        IntStream.range(0, burst.length).parallel()
                .forEach(i -> sharpness[i] = sharpness(burst[i]));
        int best = 0;
        for (int i = 0; i < burst.length; i++) {
            if (sharpness[i] > sharpness[best]) {
                best = i;
            }
        }
        List<HdrFusion.Frame> kept = new ArrayList<>();
        int ref = 0;
        StringBuilder log = new StringBuilder("Sharpness");
        for (int i = 0; i < burst.length; i++) {
            boolean keep = sharpness[i] >= KEEP_SHARPNESS * sharpness[best];
            log.append(' ').append(Math.round(sharpness[i] * 100) / 100f).append(keep ? "" : "x");
            if (i == best) {
                ref = kept.size();
            }
            if (keep) {
                kept.add(burst[i]);
            } else {
                burst[i] = null;
            }
        }
        Log.d(TAG, log + ", reference " + best);
        final HdrFusion.Frame[] frames = kept.toArray(new HdrFusion.Frame[0]);
        final int n = frames.length;
        final int refIndex = ref;
        final HdrFusion.Frame r = frames[ref];
        final int w = r.width;
        final int h = r.height;
        final int[] dx = new int[n];
        final int[] dy = new int[n];
        HdrFusion.AlignPyramid refPyr = new HdrFusion.AlignPyramid(r);
        for (int i = 0; i < n; i++) {
            if (i != refIndex) {
                int[] s = refPyr.align(frames[i]);
                dx[i] = s[0] & ~1;
                dy[i] = s[1] & ~1;
            }
        }
        refPyr = null;

        final int cw = w / 2;
        final int ch = h / 2;
        final byte[] refChromaY = new byte[cw * ch];
        for (int y = 0; y < ch; y++) {
            int row = (2 * y) * w;
            int crow = y * cw;
            for (int x = 0; x < cw; x++) {
                refChromaY[crow + x] = r.y[row + 2 * x];
            }
        }

        // Merge luma in-place into r.y (each thread only reads r.y at its own pixel p)
        IntStream.range(0, h).parallel().forEach(y -> {
            for (int x = 0; x < w; x++) {
                int p = y * w + x;
                int yr = r.y[p] & 0xff;
                int sum = yr << 8;
                int wsum = 256;
                for (int i = 0; i < n; i++) {
                    if (i == refIndex) {
                        continue;
                    }
                    int sx = clamp(x + dx[i], w);
                    int sy = clamp(y + dy[i], h);
                    int yi = frames[i].y[sy * w + sx] & 0xff;
                    int wt = WEIGHT[Math.abs(yi - yr)];
                    sum += yi * wt;
                    wsum += wt;
                }
                r.y[p] = (byte) (sum / wsum);
            }
        });
        int[] histogram = new int[256];
        for (byte b : r.y) {
            histogram[b & 0xff]++;
        }

        int med = median(histogram);
        float gain = Math.max(1f, Math.min(MAX_GAIN, TARGET_MEDIAN / Math.max(1, med)));
        Log.d(TAG, "Median " + med + ", gain " + gain);
        byte[] tone = new byte[256];
        int[] chromaScale = new int[256];
        for (int x = 0; x < 256; x++) {
            int t = Math.min(255, Math.round(x * gain / (1 + (gain - 1) * x / 255f)));
            tone[x] = (byte) t;
            chromaScale[x] = Math.round(256f * (float) Math.sqrt(t / (float) Math.max(x, 1)));
        }

        // Merge chroma in-place into r.u and r.v
        IntStream.range(0, ch).parallel().forEach(y -> {
            for (int x = 0; x < cw; x++) {
                int p = y * cw + x;
                int ym = r.y[(2 * y) * w + 2 * x] & 0xff;
                int yr = refChromaY[p] & 0xff;
                int su = (r.u[p] & 0xff) << 8;
                int sv = (r.v[p] & 0xff) << 8;
                int wsum = 256;
                for (int i = 0; i < n; i++) {
                    if (i == refIndex) {
                        continue;
                    }
                    int sx = clamp(x + dx[i] / 2, cw);
                    int sy = clamp(y + dy[i] / 2, ch);
                    int yi = frames[i].y[clamp(2 * sy, h) * w + clamp(2 * sx, w)] & 0xff;
                    int wt = WEIGHT[Math.abs(yi - yr)];
                    int q = sy * cw + sx;
                    su += (frames[i].u[q] & 0xff) * wt;
                    sv += (frames[i].v[q] & 0xff) * wt;
                    wsum += wt;
                }
                int k = chromaScale[ym];
                r.u[p] = (byte) clamp255(128 + (((su / wsum) - 128) * k >> 8));
                r.v[p] = (byte) clamp255(128 + (((sv / wsum) - 128) * k >> 8));
            }
        });
        IntStream.range(0, h).parallel().forEach(y -> {
            for (int p = y * w; p < (y + 1) * w; p++) {
                r.y[p] = tone[r.y[p] & 0xff];
            }
        });
        return r;
    }

    private static float sharpness(HdrFusion.Frame f) {
        final int w = f.width;
        final int h = f.height;
        long sum = 0;
        long count = 0;
        for (int y = 2; y < h - 2; y += 2) {
            for (int x = 2; x < w - 2; x += 2) {
                int p = y * w + x;
                int c = f.y[p] & 0xff;
                int gx = (f.y[p + 2] & 0xff) - (f.y[p - 2] & 0xff);
                int gy = (f.y[p + 2 * w] & 0xff) - (f.y[p - 2 * w] & 0xff);
                sum += gx * gx + gy * gy;
                count += c;
            }
        }
        return count > 0 ? (float) sum / count : 0f;
    }

    private static int median(int[] histogram) {
        long total = 0;
        for (int c : histogram) {
            total += c;
        }
        long seen = 0;
        for (int v = 0; v < 256; v++) {
            seen += histogram[v];
            if (seen * 2 >= total) {
                return v;
            }
        }
        return 255;
    }

    private static int clamp(int v, int size) {
        return v < 0 ? 0 : (v >= size ? size - 1 : v);
    }

    private static int clamp255(int v) {
        return v < 0 ? 0 : (v > 255 ? 255 : v);
    }
}
