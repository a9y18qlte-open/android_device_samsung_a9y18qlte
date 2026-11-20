/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package androidx.camera.extensions.impl;

import android.util.Log;

import java.util.ArrayList;
import java.util.List;

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
        int best = 0;
        for (int i = 0; i < burst.length; i++) {
            sharpness[i] = sharpness(burst[i]);
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
            }
        }
        Log.d(TAG, log + ", reference " + best);
        final HdrFusion.Frame[] frames = kept.toArray(new HdrFusion.Frame[0]);
        final int n = frames.length;
        final HdrFusion.Frame r = frames[ref];
        final int w = r.width;
        final int h = r.height;
        int[] dx = new int[n];
        int[] dy = new int[n];
        for (int i = 0; i < n; i++) {
            if (i != ref) {
                int[] s = HdrFusion.align(r, frames[i]);
                dx[i] = s[0] & ~1;
                dy[i] = s[1] & ~1;
            }
        }

        HdrFusion.Frame out = new HdrFusion.Frame(w, h);
        int[] histogram = new int[256];
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                int p = y * w + x;
                int yr = r.y[p] & 0xff;
                int sum = yr << 8;
                int wsum = 256;
                for (int i = 0; i < n; i++) {
                    if (i == ref) {
                        continue;
                    }
                    int sx = clamp(x + dx[i], w);
                    int sy = clamp(y + dy[i], h);
                    int yi = frames[i].y[sy * w + sx] & 0xff;
                    int wt = WEIGHT[Math.abs(yi - yr)];
                    sum += yi * wt;
                    wsum += wt;
                }
                int v = sum / wsum;
                out.y[p] = (byte) v;
                histogram[v]++;
            }
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

        final int cw = w / 2;
        final int ch = h / 2;
        for (int y = 0; y < ch; y++) {
            for (int x = 0; x < cw; x++) {
                int p = y * cw + x;
                int ym = out.y[(2 * y) * w + 2 * x] & 0xff;
                int yr = r.y[(2 * y) * w + 2 * x] & 0xff;
                int su = (r.u[p] & 0xff) << 8;
                int sv = (r.v[p] & 0xff) << 8;
                int wsum = 256;
                for (int i = 0; i < n; i++) {
                    if (i == ref) {
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
                out.u[p] = (byte) clamp255(128 + (((su / wsum) - 128) * k >> 8));
                out.v[p] = (byte) clamp255(128 + (((sv / wsum) - 128) * k >> 8));
            }
        }
        for (int p = 0; p < out.y.length; p++) {
            out.y[p] = tone[out.y[p] & 0xff];
        }
        return out;
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
