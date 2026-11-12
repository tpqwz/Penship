#include "image.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static unsigned long be32(const unsigned char *p) {
    return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) | ((unsigned long)p[2] << 8) | p[3];
}

static int paeth(int a, int b, int c) {
    int p = a + b - c;
    int pa = p > a ? p - a : a - p;
    int pb = p > b ? p - b : b - p;
    int pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) {
        return a;
    }
    return pb <= pc ? b : c;
}

static int unfilter(unsigned char *raw, int width, int height, int ch) {
    int stride = width * ch;
    unsigned char *prev = calloc((size_t)stride, 1);
    unsigned char *row = raw;
    if (prev == NULL || stride < 1) {
        free(prev);
        return -1;
    }
    for (int y = 0; y < height; y++) {
        int filter = row[0];
        unsigned char *pix = row + 1;
        if (filter > 4) {
            free(prev);
            return -1;
        }
        for (int i = 0; i < stride; i++) {
            int left = i >= ch ? pix[i - ch] : 0;
            int up = prev[i];
            int ul = i >= ch ? prev[i - ch] : 0;
            int v = pix[i];
            if (filter == 1) {
                v += left;
            } else if (filter == 2) {
                v += up;
            } else if (filter == 3) {
                v += (left + up) / 2;
            } else if (filter == 4) {
                v += paeth(left, up, ul);
            }
            pix[i] = (unsigned char)(v & 255);
        }
        memcpy(prev, pix, (size_t)stride);
        row += (size_t)stride + 1;
    }
    free(prev);
    return 0;
}

/* shortcut: baseline JPEG only. Progressive, arithmetic, and CMYK are refused.
   Three components are read as YCbCr. */
static const unsigned char k_zigzag[64] = {0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
                                            41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
                                            30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

static const double k_cos[8][8] = {
    {1.000000000, 1.000000000, 1.000000000, 1.000000000, 1.000000000, 1.000000000, 1.000000000, 1.000000000},
    {0.980785280, 0.831469612, 0.555570233, 0.195090322, -0.195090322, -0.555570233, -0.831469612, -0.980785280},
    {0.923879533, 0.382683432, -0.382683432, -0.923879533, -0.923879533, -0.382683432, 0.382683432, 0.923879533},
    {0.831469612, -0.195090322, -0.980785280, -0.555570233, 0.555570233, 0.980785280, 0.195090322, -0.831469612},
    {0.707106781, -0.707106781, -0.707106781, 0.707106781, 0.707106781, -0.707106781, -0.707106781, 0.707106781},
    {0.555570233, -0.980785280, 0.195090322, 0.831469612, -0.831469612, -0.195090322, 0.980785280, -0.555570233},
    {0.382683432, -0.923879533, 0.923879533, -0.382683432, -0.382683432, 0.923879533, -0.923879533, 0.382683432},
    {0.195090322, -0.555570233, 0.831469612, -0.980785280, 0.980785280, -0.831469612, 0.555570233, -0.195090322},
};

struct Huff {
    int nsym;
    int mincode[17];
    int maxcode[17];
    int valptr[17];
    unsigned char sym[256];
};

struct Comp {
    int id;
    int h;
    int v;
    int tq;
    int td;
    int ta;
    int pred;
    unsigned char *plane;
    int pw;
    int ph;
};

struct Jpeg {
    const unsigned char *p;
    const unsigned char *end;
    unsigned acc;
    int nbits;
    int restart;
    int width;
    int height;
    int ncomp;
    int maxh;
    int maxv;
    int saw_sof;
    struct Comp comp[3];
    unsigned char quant[4][64];
    int quant_ok[4];
    struct Huff dc[4];
    struct Huff ac[4];
    int dc_ok[4];
    int ac_ok[4];
};

static void jpeg_clear(struct Jpeg *j) {
    int i = 0;
    for (i = 0; i < 3; i++) {
        free(j->comp[i].plane);
        j->comp[i].plane = NULL;
    }
}

static int huff_build(struct Huff *h, const unsigned char *count, const unsigned char *sym, int nsym) {
    unsigned code = 0;
    int k = 0;
    int len = 0;
    memset(h, 0, sizeof *h);
    for (len = 1; len <= 16; len++) {
        h->maxcode[len] = -1;
    }
    for (len = 1; len <= 16; len++) {
        int n = count[len - 1];
        if (n > 0) {
            if (k > 256 - n || code + (unsigned)n > (1u << len)) {
                return -1;
            }
            h->valptr[len] = k;
            h->mincode[len] = (int)code;
            if (k + n > nsym) {
                return -1;
            }
            memcpy(h->sym + k, sym, (size_t)n);
            k += n;
            sym += n;
            h->maxcode[len] = (int)(code + (unsigned)n - 1);
            code += (unsigned)n;
        }
        code <<= 1;
    }
    h->nsym = k;
    return 0;
}

static int next_byte(struct Jpeg *j, int *out) {
    if (j->p >= j->end) {
        return -1;
    }
    *out = *j->p++;
    return 0;
}

static int next_bit(struct Jpeg *j) {
    int c = 0;
    int d = 0;
    if (j->nbits == 0) {
        if (next_byte(j, &c) != 0) {
            return -1;
        }
        if (c == 0xff) {
            do {
                if (next_byte(j, &d) != 0) {
                    return -1;
                }
            } while (d == 0xff);
            if (d != 0) {
                return -1;
            }
            c = 0xff;
        }
        j->acc = (unsigned)c;
        j->nbits = 8;
    }
    j->nbits--;
    return (int)((j->acc >> j->nbits) & 1u);
}

static int huff_decode(struct Jpeg *j, const struct Huff *h) {
    int code = next_bit(j);
    int len = 1;
    int idx = 0;
    if (code < 0) {
        return -1;
    }
    while (code > h->maxcode[len]) {
        int bit = 0;
        len++;
        if (len > 16) {
            return -1;
        }
        bit = next_bit(j);
        if (bit < 0) {
            return -1;
        }
        code = (code << 1) | bit;
    }
    idx = h->valptr[len] + (code - h->mincode[len]);
    if (idx < 0 || idx >= h->nsym) {
        return -1;
    }
    return h->sym[idx];
}

static int read_bits(struct Jpeg *j, int n, int *out) {
    int v = 0;
    int i = 0;
    if (n < 0 || n > 16) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        int bit = next_bit(j);
        if (bit < 0) {
            return -1;
        }
        v = (v << 1) | bit;
    }
    *out = v;
    return 0;
}

static int extend_bits(int v, int t) {
    if (t <= 0) {
        return 0;
    }
    if (t < 16 && v < (1 << (t - 1))) {
        v -= (1 << t) - 1;
    }
    return v;
}

static int read_restart(struct Jpeg *j, int which) {
    int c = 0;
    j->nbits = 0;
    j->acc = 0;
    if (next_byte(j, &c) != 0) {
        return -1;
    }
    while (c == 0xff) {
        if (next_byte(j, &c) != 0) {
            return -1;
        }
    }
    if (c != (0xd0 + (which & 7))) {
        return -1;
    }
    return 0;
}

static void idct8(const int *coef, unsigned char *dst) {
    double tmp[64];
    int x = 0;
    int y = 0;
    int u = 0;
    int v = 0;
    for (y = 0; y < 8; y++) {
        for (x = 0; x < 8; x++) {
            double s = 0;
            for (u = 0; u < 8; u++) {
                double cu = u == 0 ? 0.7071067811865476 : 1.0;
                s += cu * (double)coef[y * 8 + u] * k_cos[u][x];
            }
            tmp[y * 8 + x] = s;
        }
    }
    for (x = 0; x < 8; x++) {
        for (y = 0; y < 8; y++) {
            double s = 0;
            int iv = 0;
            for (v = 0; v < 8; v++) {
                double cv = v == 0 ? 0.7071067811865476 : 1.0;
                s += cv * tmp[v * 8 + x] * k_cos[v][y];
            }
            s = s / 4.0 + 128.0;
            if (s <= 0) {
                iv = 0;
            } else if (s >= 255) {
                iv = 255;
            } else {
                iv = (int)(s + 0.5);
            }
            dst[y * 8 + x] = (unsigned char)iv;
        }
    }
}

static int decode_block(struct Jpeg *j, struct Comp *comp, unsigned char *dst) {
    int coef[64];
    int k = 1;
    int cat = 0;
    int bits = 0;
    int diff = 0;
    const struct Huff *dc = NULL;
    const struct Huff *ac = NULL;
    const unsigned char *quant = NULL;
    if (comp->td < 0 || comp->td > 3 || comp->ta < 0 || comp->ta > 3 || comp->tq < 0 || comp->tq > 3) {
        return -1;
    }
    if (!j->dc_ok[comp->td] || !j->ac_ok[comp->ta] || !j->quant_ok[comp->tq]) {
        return -1;
    }
    dc = &j->dc[comp->td];
    ac = &j->ac[comp->ta];
    quant = j->quant[comp->tq];
    memset(coef, 0, sizeof coef);
    cat = huff_decode(j, dc);
    if (cat < 0 || cat > 11) {
        return -1;
    }
    if (cat > 0 && read_bits(j, cat, &bits) != 0) {
        return -1;
    }
    diff = extend_bits(bits, cat);
    if (comp->pred > 20000 || comp->pred < -20000 || diff > 20000 || diff < -20000) {
        return -1;
    }
    comp->pred += diff;
    coef[0] = comp->pred * (int)quant[0];
    while (k < 64) {
        int run = 0;
        int s = huff_decode(j, ac);
        if (s < 0) {
            return -1;
        }
        if (s == 0) {
            break;
        }
        run = s >> 4;
        cat = s & 15;
        if (cat == 0) {
            if (run != 15) {
                return -1;
            }
            k += 16;
            if (k > 64) {
                return -1;
            }
            continue;
        }
        if (cat > 10) {
            return -1;
        }
        k += run;
        if (k >= 64 || read_bits(j, cat, &bits) != 0) {
            return -1;
        }
        coef[k_zigzag[k]] = extend_bits(bits, cat) * (int)quant[k];
        k++;
    }
    idct8(coef, dst);
    return 0;
}

static int marker_of(struct Jpeg *j, int *out) {
    int c = 0;
    if (next_byte(j, &c) != 0) {
        return -1;
    }
    while (c == 0xff) {
        if (next_byte(j, &c) != 0) {
            return -1;
        }
    }
    *out = c;
    return 0;
}

static int payload_of(struct Jpeg *j, const unsigned char **data, int *len) {
    int n = 0;
    int hi = 0;
    int lo = 0;
    if (next_byte(j, &hi) != 0 || next_byte(j, &lo) != 0) {
        return -1;
    }
    n = (hi << 8) | lo;
    if (n < 2 || j->p + (n - 2) > j->end) {
        return -1;
    }
    *data = j->p;
    *len = n - 2;
    j->p += n - 2;
    return 0;
}

static int parse_dqt(struct Jpeg *j, const unsigned char *data, int len) {
    int at = 0;
    while (at < len) {
        int info = 0;
        int id = 0;
        if (at >= len) {
            return -1;
        }
        info = data[at++];
        id = info & 15;
        if ((info >> 4) != 0 || id > 3 || at + 64 > len) {
            return -1;
        }
        memcpy(j->quant[id], data + at, 64);
        j->quant_ok[id] = 1;
        at += 64;
    }
    return 0;
}

static int parse_dht(struct Jpeg *j, const unsigned char *data, int len) {
    int at = 0;
    while (at < len) {
        int info = 0;
        int cls = 0;
        int id = 0;
        int sum = 0;
        int i = 0;
        unsigned char count[16];
        if (at >= len) {
            return -1;
        }
        info = data[at++];
        cls = info >> 4;
        id = info & 15;
        if (cls > 1 || id > 3 || at + 16 > len) {
            return -1;
        }
        memcpy(count, data + at, 16);
        at += 16;
        for (i = 0; i < 16; i++) {
            if (sum > 256 - count[i]) {
                return -1;
            }
            sum += count[i];
        }
        if (at + sum > len) {
            return -1;
        }
        if (cls == 0) {
            if (huff_build(&j->dc[id], count, data + at, sum) != 0) {
                return -1;
            }
            j->dc_ok[id] = 1;
        } else {
            if (huff_build(&j->ac[id], count, data + at, sum) != 0) {
                return -1;
            }
            j->ac_ok[id] = 1;
        }
        at += sum;
    }
    return 0;
}

static int parse_sof(struct Jpeg *j, const unsigned char *data, int len) {
    int i = 0;
    int n = 0;
    if (j->saw_sof || len < 6) {
        return -1;
    }
    if (data[0] != 8) {
        return -1;
    }
    j->height = (data[1] << 8) | data[2];
    j->width = (data[3] << 8) | data[4];
    n = data[5];
    if (j->width < 1 || j->height < 1 || j->width > 4096 || j->height > 4096 || (n != 1 && n != 3) ||
        len < 6 + n * 3) {
        return -1;
    }
    j->ncomp = n;
    j->maxh = 1;
    j->maxv = 1;
    for (i = 0; i < n; i++) {
        int h = data[6 + i * 3 + 1] >> 4;
        int v = data[6 + i * 3 + 1] & 15;
        if (h < 1 || h > 4 || v < 1 || v > 4) {
            return -1;
        }
        j->comp[i].id = data[6 + i * 3];
        j->comp[i].h = h;
        j->comp[i].v = v;
        j->comp[i].tq = data[6 + i * 3 + 2];
        if (j->comp[i].tq < 0 || j->comp[i].tq > 3) {
            return -1;
        }
        if (h > j->maxh) {
            j->maxh = h;
        }
        if (v > j->maxv) {
            j->maxv = v;
        }
    }
    j->saw_sof = 1;
    return 0;
}

static int parse_sos(struct Jpeg *j, const unsigned char *data, int len) {
    int n = 0;
    int i = 0;
    int seen[3] = {0, 0, 0};
    if (!j->saw_sof || len < 1) {
        return -1;
    }
    n = data[0];
    if (n != j->ncomp || len < 4 + 2 * n) {
        return -1;
    }
    if (data[1 + 2 * n] != 0 || data[2 + 2 * n] != 63 || data[3 + 2 * n] != 0) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        int id = data[1 + i * 2];
        int tables = data[2 + i * 2];
        int c = 0;
        int found = 0;
        for (c = 0; c < j->ncomp; c++) {
            if (j->comp[c].id == id) {
                if (seen[c]) {
                    return -1;
                }
                seen[c] = 1;
                j->comp[c].td = tables >> 4;
                j->comp[c].ta = tables & 15;
                found = 1;
                break;
            }
        }
        if (!found || (tables >> 4) > 3 || (tables & 15) > 3) {
            return -1;
        }
    }
    return 0;
}

static int decode_scan(struct Jpeg *j) {
    int mcu_w = 8 * j->maxh;
    int mcu_h = 8 * j->maxv;
    int cols = 0;
    int rows = 0;
    int row = 0;
    int col = 0;
    int seen = 0;
    int rst = 0;
    int total = 0;
    int i = 0;
    if (mcu_w < 8 || mcu_h < 8 || j->width > 4096 || j->height > 4096) {
        return -1;
    }
    cols = (j->width + mcu_w - 1) / mcu_w;
    rows = (j->height + mcu_h - 1) / mcu_h;
    if (cols < 1 || rows < 1 || cols > 4096 || rows > 4096) {
        return -1;
    }
    total = cols * rows;
    for (i = 0; i < j->ncomp; i++) {
        size_t n = 0;
        if (cols > 4096 / j->comp[i].h || rows > 4096 / j->comp[i].v) {
            return -1;
        }
        j->comp[i].pw = cols * j->comp[i].h * 8;
        j->comp[i].ph = rows * j->comp[i].v * 8;
        n = (size_t)j->comp[i].pw * (size_t)j->comp[i].ph;
        if (j->comp[i].pw < 8 || j->comp[i].ph < 8 || n > (size_t)4096 * 4096) {
            return -1;
        }
        j->comp[i].plane = calloc(n, 1);
        j->comp[i].pred = 0;
        if (j->comp[i].plane == NULL) {
            return -1;
        }
    }
    for (row = 0; row < rows; row++) {
        for (col = 0; col < cols; col++) {
            for (i = 0; i < j->ncomp; i++) {
                int by = 0;
                int bx = 0;
                struct Comp *comp = &j->comp[i];
                for (by = 0; by < comp->v; by++) {
                    for (bx = 0; bx < comp->h; bx++) {
                        unsigned char block[64];
                        int ox = (col * comp->h + bx) * 8;
                        int oy = (row * comp->v + by) * 8;
                        int yy = 0;
                        if (decode_block(j, comp, block) != 0) {
                            return -1;
                        }
                        if (ox < 0 || oy < 0 || ox > comp->pw - 8 || oy > comp->ph - 8) {
                            return -1;
                        }
                        for (yy = 0; yy < 8; yy++) {
                            memcpy(comp->plane + ((size_t)(oy + yy) * (size_t)comp->pw + (size_t)ox), block + yy * 8, 8);
                        }
                    }
                }
            }
            seen++;
            if (j->restart > 0 && seen < total && seen % j->restart == 0) {
                int c = 0;
                if (read_restart(j, rst) != 0) {
                    return -1;
                }
                rst++;
                for (c = 0; c < j->ncomp; c++) {
                    j->comp[c].pred = 0;
                }
            }
        }
    }
    return 0;
}

static int jpeg_decode(const unsigned char *bytes, int n, unsigned char **rgba, int *w, int *h) {
    struct Jpeg j;
    unsigned char *out = NULL;
    size_t pixels = 0;
    int y = 0;
    int x = 0;
    memset(&j, 0, sizeof j);
    if (bytes == NULL || n < 4 || rgba == NULL || w == NULL || h == NULL) {
        return -1;
    }
    j.p = bytes;
    j.end = bytes + n;
    for (;;) {
        int m = 0;
        const unsigned char *data = NULL;
        int len = 0;
        if (marker_of(&j, &m) != 0) {
            jpeg_clear(&j);
            return -1;
        }
        if (m == 0xd8 || m == 0x01 || (m >= 0xd0 && m <= 0xd7)) {
            continue;
        }
        if (m == 0xd9) {
            break;
        }
        if (m == 0xda) {
            if (payload_of(&j, &data, &len) != 0 || parse_sos(&j, data, len) != 0 || decode_scan(&j) != 0) {
                jpeg_clear(&j);
                return -1;
            }
            break;
        }
        if (payload_of(&j, &data, &len) != 0) {
            jpeg_clear(&j);
            return -1;
        }
        if (m == 0xc0) {
            if (parse_sof(&j, data, len) != 0) {
                jpeg_clear(&j);
                return -1;
            }
        } else if (m == 0xc4) {
            if (parse_dht(&j, data, len) != 0) {
                jpeg_clear(&j);
                return -1;
            }
        } else if (m == 0xdb) {
            if (parse_dqt(&j, data, len) != 0) {
                jpeg_clear(&j);
                return -1;
            }
        } else if (m == 0xdd) {
            if (len != 2) {
                jpeg_clear(&j);
                return -1;
            }
            j.restart = (data[0] << 8) | data[1];
        } else if (m == 0xc1 || m == 0xc2 || m == 0xc3 || (m >= 0xc5 && m <= 0xc7) || (m >= 0xc9 && m <= 0xcf)) {
            jpeg_clear(&j);
            return -1;
        }
    }
    if (!j.saw_sof || j.comp[0].plane == NULL) {
        jpeg_clear(&j);
        return -1;
    }
    pixels = (size_t)j.width * (size_t)j.height;
    if (pixels > SIZE_MAX / 4) {
        jpeg_clear(&j);
        return -1;
    }
    out = malloc(pixels * 4);
    if (out == NULL) {
        jpeg_clear(&j);
        return -1;
    }
    for (y = 0; y < j.height; y++) {
        for (x = 0; x < j.width; x++) {
            int sx0 = x * j.comp[0].h / j.maxh;
            int sy0 = y * j.comp[0].v / j.maxv;
            int yv = 0;
            int cb = 0;
            int cr = 0;
            int r = 0;
            int g = 0;
            int b = 0;
            unsigned char *d = NULL;
            if (sx0 < 0 || sy0 < 0 || sx0 >= j.comp[0].pw || sy0 >= j.comp[0].ph) {
                free(out);
                jpeg_clear(&j);
                return -1;
            }
            yv = j.comp[0].plane[(size_t)sy0 * (size_t)j.comp[0].pw + (size_t)sx0];
            if (j.ncomp == 1) {
                r = g = b = yv;
            } else {
                int sx1 = x * j.comp[1].h / j.maxh;
                int sy1 = y * j.comp[1].v / j.maxv;
                int sx2 = x * j.comp[2].h / j.maxh;
                int sy2 = y * j.comp[2].v / j.maxv;
                if (sx1 < 0 || sy1 < 0 || sx2 < 0 || sy2 < 0 || sx1 >= j.comp[1].pw || sy1 >= j.comp[1].ph ||
                    sx2 >= j.comp[2].pw || sy2 >= j.comp[2].ph || j.comp[1].plane == NULL || j.comp[2].plane == NULL) {
                    free(out);
                    jpeg_clear(&j);
                    return -1;
                }
                cb = (int)j.comp[1].plane[(size_t)sy1 * (size_t)j.comp[1].pw + (size_t)sx1] - 128;
                cr = (int)j.comp[2].plane[(size_t)sy2 * (size_t)j.comp[2].pw + (size_t)sx2] - 128;
                r = (int)(yv + 1.402 * cr + 0.5);
                g = (int)(yv - 0.344136 * cb - 0.714136 * cr + 0.5);
                b = (int)(yv + 1.772 * cb + 0.5);
                if (r < 0) {
                    r = 0;
                }
                if (g < 0) {
                    g = 0;
                }
                if (b < 0) {
                    b = 0;
                }
                if (r > 255) {
                    r = 255;
                }
                if (g > 255) {
                    g = 255;
                }
                if (b > 255) {
                    b = 255;
                }
            }
            d = out + ((size_t)y * (size_t)j.width + (size_t)x) * 4;
            d[0] = (unsigned char)r;
            d[1] = (unsigned char)g;
            d[2] = (unsigned char)b;
            d[3] = 255;
        }
    }
    jpeg_clear(&j);
    *rgba = out;
    *w = j.width;
    *h = j.height;
    return 0;
}

int image_decode(const unsigned char *bytes, int n, unsigned char **rgba, int *w, int *h) {
    static const unsigned char sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    unsigned char *idat = NULL;
    size_t idat_n = 0;
    size_t idat_cap = 0;
    unsigned long width = 0;
    unsigned long height = 0;
    int depth = 0;
    int color = 0;
    int ch = 0;
    int at = 8;
    z_stream z;
    unsigned char *raw = NULL;
    unsigned char *out = NULL;
    uLongf raw_n = 0;
    if (rgba == NULL || w == NULL || h == NULL) {
        return -1;
    }
    *rgba = NULL;
    *w = 0;
    *h = 0;
    if (bytes == NULL || n < 3) {
        return -1;
    }
    if (bytes[0] == 0xff && bytes[1] == 0xd8) {
        return jpeg_decode(bytes, n, rgba, w, h);
    }
    if (n < 8 || memcmp(bytes, sig, 8) != 0) {
        return -1;
    }
    while (at + 12 <= n) {
        unsigned long len = be32(bytes + at);
        const unsigned char *type = bytes + at + 4;
        const unsigned char *data = bytes + at + 8;
        if (len > (unsigned long)(n - at - 12)) {
            free(idat);
            return -1;
        }
        if (memcmp(type, "IHDR", 4) == 0) {
            if (len < 13) {
                free(idat);
                return -1;
            }
            width = be32(data);
            height = be32(data + 4);
            depth = data[8];
            color = data[9];
            if (data[12] != 0 || width == 0 || height == 0 || width > 4096 || height > 4096) {
                free(idat);
                return -1;
            }
        } else if (memcmp(type, "IDAT", 4) == 0) {
            if (idat_n > SIZE_MAX - len) {
                free(idat);
                return -1;
            }
            if (idat_n + len > idat_cap) {
                size_t cap = idat_cap == 0 ? (size_t)len + 64 : idat_cap * 2;
                unsigned char *grown;
                if (cap < idat_n + len) {
                    cap = idat_n + len;
                }
                grown = realloc(idat, cap);
                if (grown == NULL) {
                    free(idat);
                    return -1;
                }
                idat = grown;
                idat_cap = cap;
            }
            memcpy(idat + idat_n, data, (size_t)len);
            idat_n += (size_t)len;
        } else if (memcmp(type, "IEND", 4) == 0) {
            break;
        }
        at += 12 + (int)len;
    }
    if (depth != 8 || (color != 2 && color != 6) || idat == NULL || width == 0 || height == 0) {
        free(idat);
        return -1;
    }
    ch = color == 6 ? 4 : 3;
    if (width > (4096u * 4u) / (unsigned)ch) {
        free(idat);
        return -1;
    }
    raw_n = (uLongf)((width * (unsigned)ch + 1) * height);
    raw = malloc((size_t)raw_n);
    if (raw == NULL) {
        free(idat);
        return -1;
    }
    memset(&z, 0, sizeof z);
    if (inflateInit(&z) != Z_OK) {
        free(idat);
        free(raw);
        return -1;
    }
    z.next_in = idat;
    z.avail_in = (uInt)idat_n;
    z.next_out = raw;
    z.avail_out = (uInt)raw_n;
    if (inflate(&z, Z_FINISH) != Z_STREAM_END || unfilter(raw, (int)width, (int)height, ch) != 0) {
        inflateEnd(&z);
        free(idat);
        free(raw);
        return -1;
    }
    inflateEnd(&z);
    free(idat);
    out = malloc((size_t)width * (size_t)height * 4);
    if (out == NULL) {
        free(raw);
        return -1;
    }
    {
        int stride = (int)width * ch;
        for (unsigned long y = 0; y < height; y++) {
            unsigned char *pix = raw + y * (size_t)(stride + 1) + 1;
            for (unsigned long x = 0; x < width; x++) {
                unsigned char *d = out + (y * width + x) * 4;
                unsigned char *s = pix + x * (unsigned)ch;
                d[0] = s[0];
                d[1] = s[1];
                d[2] = s[2];
                d[3] = ch == 4 ? s[3] : 255;
            }
        }
    }
    free(raw);
    *rgba = out;
    *w = (int)width;
    *h = (int)height;
    return 0;
}
