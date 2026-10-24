/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package androidx.camera.extensions.impl;

import android.media.Image;

import java.nio.ByteBuffer;

/**
 * Exposure fusion (Mertens, Kautz and Van Reeth, 2007) of an exposure bracket of YUV frames.
 *
 * <p>The frames are aligned to the reference by a global translation found with median
 * threshold bitmaps (Ward, 2003), which do not depend on exposure. The Laplacian pyramid
 * fusion runs on a 1/4 scale copy of the luma; at full size every pixel is a blend of the
 * frames with smoothed weights, plus the upscaled difference between the pyramid result and
 * that blend. This keeps the local contrast of the pyramid fusion without building
 * full-size pyramids.
 */
final class HdrFusion {
    /** One YUV 4:2:0 frame, planar, chroma at half size. */
    static final class Frame {
        final int width;
        final int height;
        final byte[] y;
        final byte[] u;
        final byte[] v;

        Frame(int width, int height) {
            this.width = width;
            this.height = height;
            y = new byte[width * height];
            u = new byte[(width / 2) * (height / 2)];
            v = new byte[(width / 2) * (height / 2)];
        }
    }

    private static final int SCALE = 4;
    // well-exposedness: gaussian around mid grey
    private static final float SIGMA = 0.2f;
    // frames that differ from the reference by more than this (after brightness matching)
    // are treated as moving content there
    private static final float GHOST_SIGMA = 24f;
    private static final int SMOOTH_RADIUS = 8;

    private HdrFusion() {
    }

    static Frame fromImage(Image image) {
        int w = image.getWidth() & ~1;
        int h = image.getHeight() & ~1;
        Frame f = new Frame(w, h);
        Image.Plane[] planes = image.getPlanes();
        readPlane(planes[0], f.y, w, h);
        readPlane(planes[1], f.u, w / 2, h / 2);
        readPlane(planes[2], f.v, w / 2, h / 2);
        return f;
    }

    static void toImage(Frame f, Image image) {
        Image.Plane[] planes = image.getPlanes();
        writePlane(planes[0], f.y, f.width, f.height);
        writePlane(planes[1], f.u, f.width / 2, f.height / 2);
        writePlane(planes[2], f.v, f.width / 2, f.height / 2);
    }

    private static void readPlane(Image.Plane plane, byte[] dst, int w, int h) {
        ByteBuffer buf = plane.getBuffer();
        int rowStride = plane.getRowStride();
        int pixelStride = plane.getPixelStride();
        byte[] row = new byte[rowStride];
        for (int r = 0; r < h; r++) {
            int len = Math.min(rowStride, buf.capacity() - r * rowStride);
            buf.position(r * rowStride);
            buf.get(row, 0, len);
            if (pixelStride == 1) {
                System.arraycopy(row, 0, dst, r * w, w);
            } else {
                for (int c = 0, o = r * w; c < w; c++) {
                    dst[o + c] = row[c * pixelStride];
                }
            }
        }
    }

    private static void writePlane(Image.Plane plane, byte[] src, int w, int h) {
        ByteBuffer buf = plane.getBuffer();
        int rowStride = plane.getRowStride();
        int pixelStride = plane.getPixelStride();
        for (int r = 0; r < h; r++) {
            if (pixelStride == 1) {
                buf.position(r * rowStride);
                buf.put(src, r * w, w);
            } else {
                // semi-planar chroma: the other plane shares these bytes, so write each
                // sample on its own
                for (int c = 0, o = r * w, p = r * rowStride; c < w; c++) {
                    buf.put(p + c * pixelStride, src[o + c]);
                }
            }
        }
    }

    /**
     * Fuses the frames into a new frame. All frames must have the same size; ref is the
     * index of the frame the others are aligned to.
     */
    static Frame fuse(Frame[] frames, int ref) {
        final int n = frames.length;
        final int w = frames[ref].width;
        final int h = frames[ref].height;

        // global alignment to the reference, in full size pixels (kept even for chroma)
        int[] dx = new int[n];
        int[] dy = new int[n];
        for (int i = 0; i < n; i++) {
            if (i != ref) {
                int[] s = align(frames[ref], frames[i]);
                dx[i] = s[0] & ~1;
                dy[i] = s[1] & ~1;
            }
        }

        // low resolution luma of each aligned frame
        final int lw = w / SCALE;
        final int lh = h / SCALE;
        float[][] low = new float[n][];
        for (int i = 0; i < n; i++) {
            low[i] = downscale(frames[i].y, w, h, SCALE, dx[i], dy[i]);
        }

        // weights: well-exposedness, times consistency with the reference
        float[][] weight = new float[n][lw * lh];
        for (int i = 0; i < n; i++) {
            float[] map = i == ref ? null : matchHistogram(low[ref], low[i]);
            float[] wi = weight[i];
            float[] li = low[i];
            float[] lr = low[ref];
            for (int p = 0; p < wi.length; p++) {
                float x = li[p] / 255f - 0.5f;
                float e = (float) Math.exp(-x * x / (2 * SIGMA * SIGMA));
                if (map != null && lr[p] > 16 && lr[p] < 240) {
                    float d = li[p] - map[(int) lr[p]];
                    e *= (float) Math.exp(-d * d / (2 * GHOST_SIGMA * GHOST_SIGMA));
                }
                wi[p] = e + 1e-4f;
            }
        }
        normalize(weight);

        // pyramid fusion at low resolution
        float[] fused = pyramidFuse(low, weight, lw, lh);

        // smooth weights for the full size blend, and the blend at low resolution
        float[][] smooth = new float[n][];
        for (int i = 0; i < n; i++) {
            smooth[i] = boxBlur(weight[i], lw, lh, SMOOTH_RADIUS, 3);
        }
        normalize(smooth);
        float[] corr = new float[lw * lh];
        for (int p = 0; p < corr.length; p++) {
            float b = 0;
            for (int i = 0; i < n; i++) {
                b += smooth[i][p] * low[i][p];
            }
            corr[p] = fused[p] - b;
        }

        Frame out = new Frame(w, h);
        blendLuma(frames, smooth, corr, dx, dy, lw, lh, out);
        blendChroma(frames, smooth, dx, dy, lw, lh, out);
        return out;
    }

    // ---- alignment (median threshold bitmaps) ----

    private static int[] align(Frame ref, Frame f) {
        // pyramid of 1/2 size luma, coarsest level ~ 64 pixels high
        int levels = 0;
        for (int s = Math.min(ref.width, ref.height) / 2; s > 64; s /= 2) {
            levels++;
        }
        byte[][] pa = new byte[levels + 1][];
        byte[][] pb = new byte[levels + 1][];
        int[] pw = new int[levels + 1];
        int[] ph = new int[levels + 1];
        pa[0] = half(ref.y, ref.width, ref.height);
        pb[0] = half(f.y, f.width, f.height);
        pw[0] = ref.width / 2;
        ph[0] = ref.height / 2;
        for (int l = 1; l <= levels; l++) {
            pa[l] = half(pa[l - 1], pw[l - 1], ph[l - 1]);
            pb[l] = half(pb[l - 1], pw[l - 1], ph[l - 1]);
            pw[l] = pw[l - 1] / 2;
            ph[l] = ph[l - 1] / 2;
        }
        int sx = 0;
        int sy = 0;
        for (int l = levels; l >= 0; l--) {
            sx *= 2;
            sy *= 2;
            int ma = median(pa[l]);
            int mb = median(pb[l]);
            long best = Long.MAX_VALUE;
            int bx = 0;
            int by = 0;
            for (int oy = -1; oy <= 1; oy++) {
                for (int ox = -1; ox <= 1; ox++) {
                    long e = mtbError(pa[l], pb[l], pw[l], ph[l], ma, mb, sx + ox, sy + oy);
                    if (e < best) {
                        best = e;
                        bx = ox;
                        by = oy;
                    }
                }
            }
            sx += bx;
            sy += by;
        }
        // level 0 is half size
        return new int[] {sx * 2, sy * 2};
    }

    private static byte[] half(byte[] src, int w, int h) {
        int hw = w / 2;
        int hh = h / 2;
        byte[] dst = new byte[hw * hh];
        for (int r = 0; r < hh; r++) {
            int a = 2 * r * w;
            int b = a + w;
            for (int c = 0; c < hw; c++) {
                int s = (src[a + 2 * c] & 0xff) + (src[a + 2 * c + 1] & 0xff)
                        + (src[b + 2 * c] & 0xff) + (src[b + 2 * c + 1] & 0xff);
                dst[r * hw + c] = (byte) (s >> 2);
            }
        }
        return dst;
    }

    private static int median(byte[] img) {
        int[] hist = new int[256];
        for (byte b : img) {
            hist[b & 0xff]++;
        }
        int half = img.length / 2;
        int sum = 0;
        for (int i = 0; i < 256; i++) {
            sum += hist[i];
            if (sum >= half) {
                return i;
            }
        }
        return 128;
    }

    /** Pixels whose threshold bits differ, b shifted by (sx, sy); near-median pixels skipped. */
    private static long mtbError(byte[] a, byte[] b, int w, int h, int ma, int mb,
            int sx, int sy) {
        final int noise = 4;
        long err = 0;
        int x0 = Math.max(0, -sx);
        int x1 = Math.min(w, w - sx);
        int y0 = Math.max(0, -sy);
        int y1 = Math.min(h, h - sy);
        for (int r = y0; r < y1; r++) {
            int ia = r * w;
            int ib = (r + sy) * w + sx;
            for (int c = x0; c < x1; c++) {
                int va = a[ia + c] & 0xff;
                int vb = b[ib + c] & 0xff;
                if (Math.abs(va - ma) <= noise || Math.abs(vb - mb) <= noise) {
                    continue;
                }
                if ((va > ma) != (vb > mb)) {
                    err++;
                }
            }
        }
        return err;
    }

    // ---- low resolution helpers ----

    /** Box downscale by s of a frame shifted by (dx, dy) (pixel p reads p + d). */
    private static float[] downscale(byte[] src, int w, int h, int s, int dx, int dy) {
        int lw = w / s;
        int lh = h / s;
        float[] dst = new float[lw * lh];
        float norm = 1f / (s * s);
        for (int r = 0; r < lh; r++) {
            for (int c = 0; c < lw; c++) {
                int sum = 0;
                for (int j = 0; j < s; j++) {
                    int y = clamp(r * s + j + dy, h);
                    int o = y * w;
                    for (int i = 0; i < s; i++) {
                        sum += src[o + clamp(c * s + i + dx, w)] & 0xff;
                    }
                }
                dst[r * lw + c] = sum * norm;
            }
        }
        return dst;
    }

    /** For each value of ref, the value of f at the same rank (brightness matching). */
    private static float[] matchHistogram(float[] ref, float[] f) {
        int[] hr = new int[256];
        int[] hf = new int[256];
        for (int p = 0; p < ref.length; p++) {
            hr[(int) ref[p]]++;
            hf[(int) f[p]]++;
        }
        float[] map = new float[256];
        long cr = 0;
        long cf = 0;
        int j = 0;
        for (int i = 0; i < 256; i++) {
            cr += hr[i];
            while (j < 255 && cf + hf[j] < cr) {
                cf += hf[j];
                j++;
            }
            map[i] = j;
        }
        return map;
    }

    private static void normalize(float[][] w) {
        int len = w[0].length;
        for (int p = 0; p < len; p++) {
            float s = 0;
            for (float[] wi : w) {
                s += wi[p];
            }
            for (float[] wi : w) {
                wi[p] /= s;
            }
        }
    }

    private static float[] boxBlur(float[] src, int w, int h, int r, int passes) {
        float[] a = src.clone();
        float[] b = new float[a.length];
        for (int pass = 0; pass < passes; pass++) {
            // horizontal
            for (int y = 0; y < h; y++) {
                int o = y * w;
                float sum = 0;
                for (int x = -r; x <= r; x++) {
                    sum += a[o + clamp(x, w)];
                }
                for (int x = 0; x < w; x++) {
                    b[o + x] = sum / (2 * r + 1);
                    sum += a[o + clamp(x + r + 1, w)] - a[o + clamp(x - r, w)];
                }
            }
            // vertical
            for (int x = 0; x < w; x++) {
                float sum = 0;
                for (int y = -r; y <= r; y++) {
                    sum += b[clamp(y, h) * w + x];
                }
                for (int y = 0; y < h; y++) {
                    a[y * w + x] = sum / (2 * r + 1);
                    sum += b[clamp(y + r + 1, h) * w + x] - b[clamp(y - r, h) * w + x];
                }
            }
        }
        return a;
    }

    // ---- Laplacian pyramid fusion ----

    private static float[] pyramidFuse(float[][] img, float[][] weight, int w, int h) {
        int levels = 1;
        for (int s = Math.min(w, h); s > 16; s /= 2) {
            levels++;
        }
        int[] lw = new int[levels];
        int[] lh = new int[levels];
        lw[0] = w;
        lh[0] = h;
        for (int l = 1; l < levels; l++) {
            lw[l] = (lw[l - 1] + 1) / 2;
            lh[l] = (lh[l - 1] + 1) / 2;
        }
        float[][] acc = new float[levels][];
        for (int l = 0; l < levels; l++) {
            acc[l] = new float[lw[l] * lh[l]];
        }
        for (int i = 0; i < img.length; i++) {
            float[] g = img[i];
            float[] wg = weight[i];
            for (int l = 0; l < levels; l++) {
                float[] lap;
                float[] gNext = null;
                float[] wNext = null;
                if (l < levels - 1) {
                    gNext = reduce(g, lw[l], lh[l]);
                    wNext = reduce(wg, lw[l], lh[l]);
                    float[] up = expand(gNext, lw[l + 1], lh[l + 1], lw[l], lh[l]);
                    lap = new float[g.length];
                    for (int p = 0; p < g.length; p++) {
                        lap[p] = g[p] - up[p];
                    }
                } else {
                    lap = g;
                }
                float[] a = acc[l];
                for (int p = 0; p < a.length; p++) {
                    a[p] += wg[p] * lap[p];
                }
                g = gNext;
                wg = wNext;
            }
        }
        float[] r = acc[levels - 1];
        for (int l = levels - 2; l >= 0; l--) {
            float[] up = expand(r, lw[l + 1], lh[l + 1], lw[l], lh[l]);
            for (int p = 0; p < up.length; p++) {
                up[p] += acc[l][p];
            }
            r = up;
        }
        return r;
    }

    /** 5 tap binomial blur, then every other pixel. */
    private static float[] reduce(float[] src, int w, int h) {
        int nw = (w + 1) / 2;
        int nh = (h + 1) / 2;
        float[] tmp = new float[nw * h];
        for (int y = 0; y < h; y++) {
            int o = y * w;
            for (int x = 0; x < nw; x++) {
                int c = 2 * x;
                tmp[y * nw + x] = (src[o + clamp(c - 2, w)] + 4 * src[o + clamp(c - 1, w)]
                        + 6 * src[o + c] + 4 * src[o + clamp(c + 1, w)]
                        + src[o + clamp(c + 2, w)]) / 16f;
            }
        }
        float[] dst = new float[nw * nh];
        for (int y = 0; y < nh; y++) {
            int c = 2 * y;
            for (int x = 0; x < nw; x++) {
                dst[y * nw + x] = (tmp[clamp(c - 2, h) * nw + x] + 4 * tmp[clamp(c - 1, h) * nw + x]
                        + 6 * tmp[c * nw + x] + 4 * tmp[clamp(c + 1, h) * nw + x]
                        + tmp[clamp(c + 2, h) * nw + x]) / 16f;
            }
        }
        return dst;
    }

    /** Bilinear upscale of a reduced level back to (w, h). */
    private static float[] expand(float[] src, int sw, int sh, int w, int h) {
        float[] dst = new float[w * h];
        for (int y = 0; y < h; y++) {
            float fy = y * 0.5f;
            int y0 = Math.min((int) fy, sh - 1);
            int y1 = Math.min(y0 + 1, sh - 1);
            float ay = fy - y0;
            for (int x = 0; x < w; x++) {
                float fx = x * 0.5f;
                int x0 = Math.min((int) fx, sw - 1);
                int x1 = Math.min(x0 + 1, sw - 1);
                float ax = fx - x0;
                float top = src[y0 * sw + x0] * (1 - ax) + src[y0 * sw + x1] * ax;
                float bot = src[y1 * sw + x0] * (1 - ax) + src[y1 * sw + x1] * ax;
                dst[y * w + x] = top * (1 - ay) + bot * ay;
            }
        }
        return dst;
    }

    // ---- full size blend ----

    private static void blendLuma(Frame[] frames, float[][] smooth, float[] corr,
            int[] dx, int[] dy, int lw, int lh, Frame out) {
        final int n = frames.length;
        final int w = out.width;
        final int h = out.height;
        float[] wt = new float[n];
        // per row: low resolution sample positions, centres of the SCALE x SCALE boxes
        int[] cx0 = new int[w];
        float[] cax = new float[w];
        for (int x = 0; x < w; x++) {
            float fx = Math.max(0f, Math.min(lw - 1f, (x + 0.5f) / SCALE - 0.5f));
            cx0[x] = Math.min((int) fx, lw - 2);
            cax[x] = fx - cx0[x];
        }
        for (int y = 0; y < h; y++) {
            float fy = Math.max(0f, Math.min(lh - 1f, (y + 0.5f) / SCALE - 0.5f));
            int y0 = Math.min((int) fy, lh - 2);
            float ay = fy - y0;
            int r0 = y0 * lw;
            int r1 = r0 + lw;
            int[] rows = new int[n];
            for (int i = 0; i < n; i++) {
                rows[i] = clamp(y + dy[i], h) * w;
            }
            for (int x = 0; x < w; x++) {
                int x0 = cx0[x];
                float ax = cax[x];
                float k00 = (1 - ax) * (1 - ay);
                float k01 = ax * (1 - ay);
                float k10 = (1 - ax) * ay;
                float k11 = ax * ay;
                float v = corr[r0 + x0] * k00 + corr[r0 + x0 + 1] * k01
                        + corr[r1 + x0] * k10 + corr[r1 + x0 + 1] * k11;
                for (int i = 0; i < n; i++) {
                    float[] s = smooth[i];
                    wt[i] = s[r0 + x0] * k00 + s[r0 + x0 + 1] * k01
                            + s[r1 + x0] * k10 + s[r1 + x0 + 1] * k11;
                    v += wt[i] * (frames[i].y[rows[i] + clamp(x + dx[i], w)] & 0xff);
                }
                out.y[y * w + x] = (byte) clamp255(v);
            }
        }
    }

    private static void blendChroma(Frame[] frames, float[][] smooth, int[] dx, int[] dy,
            int lw, int lh, Frame out) {
        final int n = frames.length;
        final int cw = out.width / 2;
        final int ch = out.height / 2;
        final float cs = SCALE / 2f;
        for (int y = 0; y < ch; y++) {
            float fy = Math.max(0f, Math.min(lh - 1f, (y + 0.5f) / cs - 0.5f));
            int y0 = Math.min((int) fy, lh - 2);
            float ay = fy - y0;
            for (int x = 0; x < cw; x++) {
                float fx = Math.max(0f, Math.min(lw - 1f, (x + 0.5f) / cs - 0.5f));
                int x0 = Math.min((int) fx, lw - 2);
                float ax = fx - x0;
                int p = y0 * lw + x0;
                float u = 0;
                float v = 0;
                for (int i = 0; i < n; i++) {
                    float[] s = smooth[i];
                    float wt = s[p] * (1 - ax) * (1 - ay) + s[p + 1] * ax * (1 - ay)
                            + s[p + lw] * (1 - ax) * ay + s[p + lw + 1] * ax * ay;
                    int q = clamp(y + dy[i] / 2, ch) * cw + clamp(x + dx[i] / 2, cw);
                    u += wt * ((frames[i].u[q] & 0xff) - 128);
                    v += wt * ((frames[i].v[q] & 0xff) - 128);
                }
                out.u[y * cw + x] = (byte) clamp255(u + 128);
                out.v[y * cw + x] = (byte) clamp255(v + 128);
            }
        }
    }

    private static int clamp(int v, int size) {
        return v < 0 ? 0 : (v >= size ? size - 1 : v);
    }

    private static int clamp255(float v) {
        int i = Math.round(v);
        return i < 0 ? 0 : (i > 255 ? 255 : i);
    }
}
