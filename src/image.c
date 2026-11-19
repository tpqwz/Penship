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

static int unfilter(unsigned char *raw, int height, int stride, int bpp) {
    unsigned char *prev = calloc((size_t)stride, 1);
    unsigned char *row = raw;
    if (prev == NULL || stride < 1 || bpp < 1 || bpp > stride) {
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
            int left = i >= bpp ? pix[i - bpp] : 0;
            int up = prev[i];
            int ul = i >= bpp ? prev[i - bpp] : 0;
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

/* shortcut: arithmetic and CMYK JPEG are refused. Three components are read as YCbCr.
   A progressive file keeps one coefficient grid per component and fills it scan by scan. */
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
    short *coef;
    int pw;
    int ph;
    int nbx;
    int nby;
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
    int progressive;
    int ss;
    int se;
    int ah;
    int al;
    int eob_run;
    int scan_n;
    int scan_comp[3];
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
        free(j->comp[i].coef);
        j->comp[i].plane = NULL;
        j->comp[i].coef = NULL;
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
    if (n < 1 || n > j->ncomp || len < 4 + 2 * n) {
        return -1;
    }
    j->ss = data[1 + 2 * n];
    j->se = data[2 + 2 * n];
    j->ah = data[3 + 2 * n] >> 4;
    j->al = data[3 + 2 * n] & 15;
    if (j->ss > j->se || j->se > 63 || j->ah > 13 || j->al > 13) {
        return -1;
    }
    if (!j->progressive) {
        if (n != j->ncomp || j->ss != 0 || j->se != 63 || j->ah != 0 || j->al != 0) {
            return -1;
        }
    } else if (j->ss == 0) {
        if (j->ah != 0 && j->se != 0) {
            return -1;
        }
        if (j->se != 0 && n != 1) {
            return -1;
        }
    } else if (n != 1) {
        return -1;
    }
    j->scan_n = n;
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
                j->scan_comp[i] = c;
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

/* Blocks actually stored for one component. A partial last MCU does not invent extra blocks. */
static int blocks_in(int size, int samp, int maxs) {
    int denom = 8 * maxs;
    if (size < 1 || samp < 1 || maxs < 1 || denom < 8) {
        return 0;
    }
    return (size * samp + denom - 1) / denom;
}

static int mcu_grid(const struct Jpeg *j, int *cols, int *rows) {
    int mcu_w = 8 * j->maxh;
    int mcu_h = 8 * j->maxv;
    if (mcu_w < 8 || mcu_h < 8 || j->width > 4096 || j->height > 4096) {
        return -1;
    }
    *cols = (j->width + mcu_w - 1) / mcu_w;
    *rows = (j->height + mcu_h - 1) / mcu_h;
    if (*cols < 1 || *rows < 1 || *cols > 4096 || *rows > 4096) {
        return -1;
    }
    return 0;
}

static int alloc_coef(struct Jpeg *j) {
    int cols = 0;
    int rows = 0;
    int i = 0;
    if (mcu_grid(j, &cols, &rows) != 0) {
        return -1;
    }
    for (i = 0; i < j->ncomp; i++) {
        size_t blocks = 0;
        j->comp[i].nbx = blocks_in(j->width, j->comp[i].h, j->maxh);
        j->comp[i].nby = blocks_in(j->height, j->comp[i].v, j->maxv);
        if (j->comp[i].nbx < 1 || j->comp[i].nby < 1) {
            return -1;
        }
        blocks = (size_t)j->comp[i].nbx * (size_t)j->comp[i].nby;
        if (blocks > SIZE_MAX / 64) {
            return -1;
        }
        j->comp[i].coef = calloc(blocks * 64, sizeof(short));
        if (j->comp[i].coef == NULL) {
            return -1;
        }
    }
    return 0;
}

static int refine_coef(struct Jpeg *j, short *coef) {
    int bit = next_bit(j);
    int step = 0;
    if (bit < 0 || j->al < 0 || j->al > 14) {
        return -1;
    }
    if (!bit) {
        return 0;
    }
    step = 1 << j->al;
    if (*coef > 0) {
        if (*coef > 32767 - step) {
            return -1;
        }
        *coef = (short)(*coef + step);
    } else if (*coef < 0) {
        if (*coef < -32767 + step) {
            return -1;
        }
        *coef = (short)(*coef - step);
    }
    return 0;
}

static int put_mag(short *dst, int mag) {
    if (mag > 32767 || mag < -32767) {
        return -1;
    }
    *dst = (short)mag;
    return 0;
}

/* A negative magnitude cannot be shifted with <<. The coefficient stays inside a short. */
static int shl_coef(int mag, int al) {
    int neg = 0;
    unsigned u = 0;
    if (al < 0 || al > 14) {
        return 0;
    }
    if (mag < 0) {
        neg = 1;
        mag = -mag;
    }
    if (al > 0 && mag > (32767 >> al)) {
        return neg ? -32767 : 32767;
    }
    u = ((unsigned)mag) << al;
    return neg ? -(int)u : (int)u;
}

/* One block of a progressive scan. DC and AC, first pass and refinement, share this. */
static int decode_prog_block(struct Jpeg *j, struct Comp *comp, short *data) {
    int k = j->ss;
    if (j->ah == 0 && j->ss == 0) {
        int cat = 0;
        int bits = 0;
        int diff = 0;
        int mag = 0;
        if (comp->td < 0 || comp->td > 3 || !j->dc_ok[comp->td]) {
            return -1;
        }
        cat = huff_decode(j, &j->dc[comp->td]);
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
        mag = shl_coef(comp->pred, j->al);
        if (put_mag(&data[0], mag) != 0) {
            return -1;
        }
        if (j->se == 0) {
            return 0;
        }
        k = 1;
    } else if (j->ah != 0 && j->ss == 0) {
        return refine_coef(j, &data[0]);
    }
    if (comp->ta < 0 || comp->ta > 3 || !j->ac_ok[comp->ta]) {
        return -1;
    }
    if (j->ah == 0) {
        if (j->eob_run > 0) {
            j->eob_run--;
            return 0;
        }
        while (k <= j->se) {
            int s = huff_decode(j, &j->ac[comp->ta]);
            int run = 0;
            int cat = 0;
            int bits = 0;
            if (s < 0) {
                return -1;
            }
            run = s >> 4;
            cat = s & 15;
            if (cat != 0) {
                if (cat > 10) {
                    return -1;
                }
                k += run;
                if (k > j->se || read_bits(j, cat, &bits) != 0) {
                    return -1;
                }
                if (put_mag(&data[k_zigzag[k]], shl_coef(extend_bits(bits, cat), j->al)) != 0) {
                    return -1;
                }
                k++;
            } else if (run == 15) {
                k += 16;
            } else {
                int extra = 0;
                if (run > 0 && read_bits(j, run, &extra) != 0) {
                    return -1;
                }
                j->eob_run = ((1 << run) + extra) - 1;
                break;
            }
        }
        return k > j->se + 16 ? -1 : 0;
    }
    if (j->eob_run > 0) {
        while (k <= j->se) {
            int nat = k_zigzag[k];
            if (data[nat] != 0 && refine_coef(j, &data[nat]) != 0) {
                return -1;
            }
            k++;
        }
        j->eob_run--;
        return 0;
    }
    while (k <= j->se) {
        int s = huff_decode(j, &j->ac[comp->ta]);
        int run = 0;
        int cat = 0;
        int newv = 0;
        int place = 0;
        if (s < 0) {
            return -1;
        }
        run = s >> 4;
        cat = s & 15;
        if (cat != 0) {
            int sign = 0;
            if (cat != 1) {
                return -1;
            }
            sign = next_bit(j);
            if (sign < 0) {
                return -1;
            }
            newv = sign ? (1 << j->al) : -(1 << j->al);
            place = 1;
        } else if (run < 15) {
            int extra = 0;
            if (run > 0 && read_bits(j, run, &extra) != 0) {
                return -1;
            }
            j->eob_run = (1 << run) + extra;
            run = 0;
            place = 0;
        } else {
            run = 16;
        }
        while (k <= j->se) {
            int nat = k_zigzag[k];
            if (data[nat] != 0) {
                if (refine_coef(j, &data[nat]) != 0) {
                    return -1;
                }
                k++;
                continue;
            }
            if (run == 0) {
                if (place) {
                    data[nat] = (short)newv;
                    k++;
                }
                break;
            }
            run--;
            k++;
            if (run == 0 && !place) {
                break;
            }
        }
        if (place && k > j->se + 1) {
            return -1;
        }
        if (j->eob_run > 0) {
            while (k <= j->se) {
                int nat = k_zigzag[k];
                if (data[nat] != 0 && refine_coef(j, &data[nat]) != 0) {
                    return -1;
                }
                k++;
            }
            j->eob_run--;
            return 0;
        }
    }
    return 0;
}

static int decode_scan_prog(struct Jpeg *j) {
    int cols = 0;
    int rows = 0;
    int seen = 0;
    int rst = 0;
    int total = 0;
    int i = 0;
    if (mcu_grid(j, &cols, &rows) != 0) {
        return -1;
    }
    j->nbits = 0;
    j->acc = 0;
    j->eob_run = 0;
    if (j->ss == 0 && j->ah == 0) {
        for (i = 0; i < j->scan_n; i++) {
            j->comp[j->scan_comp[i]].pred = 0;
        }
    }
    if (j->scan_n > 1) {
        total = cols * rows;
        for (int row = 0; row < rows; row++) {
            for (int col = 0; col < cols; col++) {
                for (i = 0; i < j->scan_n; i++) {
                    struct Comp *comp = &j->comp[j->scan_comp[i]];
                    int by = 0;
                    int bx = 0;
                    for (by = 0; by < comp->v; by++) {
                        for (bx = 0; bx < comp->h; bx++) {
                            int x = col * comp->h + bx;
                            int y = row * comp->v + by;
                            short scratch[64];
                            short *data = scratch;
                            memset(scratch, 0, sizeof scratch);
                            if (x >= 0 && y >= 0 && x < comp->nbx && y < comp->nby) {
                                data = comp->coef + ((size_t)y * (size_t)comp->nbx + (size_t)x) * 64;
                            }
                            if (decode_prog_block(j, comp, data) != 0) {
                                return -1;
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
                    j->eob_run = 0;
                    for (c = 0; c < j->scan_n; c++) {
                        j->comp[j->scan_comp[c]].pred = 0;
                    }
                }
            }
        }
    } else {
        struct Comp *comp = &j->comp[j->scan_comp[0]];
        total = comp->nbx * comp->nby;
        for (i = 0; i < total; i++) {
            if (decode_prog_block(j, comp, comp->coef + (size_t)i * 64) != 0) {
                return -1;
            }
            if (j->restart > 0 && (i + 1) < total && (i + 1) % j->restart == 0) {
                if (read_restart(j, rst) != 0) {
                    return -1;
                }
                rst++;
                j->eob_run = 0;
                comp->pred = 0;
            }
        }
    }
    j->nbits = 0;
    j->acc = 0;
    return 0;
}

static int paint_coef(struct Jpeg *j) {
    int i = 0;
    for (i = 0; i < j->ncomp; i++) {
        struct Comp *comp = &j->comp[i];
        const unsigned char *quant = NULL;
        int by = 0;
        int bx = 0;
        size_t n = 0;
        if (comp->tq < 0 || comp->tq > 3 || !j->quant_ok[comp->tq] || comp->coef == NULL) {
            return -1;
        }
        quant = j->quant[comp->tq];
        comp->pw = comp->nbx * 8;
        comp->ph = comp->nby * 8;
        n = (size_t)comp->pw * (size_t)comp->ph;
        if (comp->pw < 8 || comp->ph < 8 || n > (size_t)4096 * 4096) {
            return -1;
        }
        comp->plane = calloc(n, 1);
        if (comp->plane == NULL) {
            return -1;
        }
        for (by = 0; by < comp->nby; by++) {
            for (bx = 0; bx < comp->nbx; bx++) {
                short *src = comp->coef + ((size_t)by * (size_t)comp->nbx + (size_t)bx) * 64;
                int coef[64];
                unsigned char block[64];
                int k = 0;
                int oy = by * 8;
                int ox = bx * 8;
                int yy = 0;
                for (k = 0; k < 64; k++) {
                    int nat = k_zigzag[k];
                    coef[nat] = (int)src[nat] * (int)quant[k];
                }
                idct8(coef, block);
                for (yy = 0; yy < 8; yy++) {
                    memcpy(comp->plane + ((size_t)(oy + yy) * (size_t)comp->pw + (size_t)ox), block + yy * 8, 8);
                }
            }
        }
    }
    return 0;
}

static int blocks_in(int size, int samp, int maxs);

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
        j->comp[i].nbx = blocks_in(j->width, j->comp[i].h, j->maxh);
        j->comp[i].nby = blocks_in(j->height, j->comp[i].v, j->maxv);
        if (j->comp[i].nbx < 1 || j->comp[i].nby < 1) {
            return -1;
        }
        j->comp[i].pw = j->comp[i].nbx * 8;
        j->comp[i].ph = j->comp[i].nby * 8;
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
                        if (ox / 8 >= comp->nbx || oy / 8 >= comp->nby) {
                            continue;
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
            if (payload_of(&j, &data, &len) != 0 || parse_sos(&j, data, len) != 0) {
                jpeg_clear(&j);
                return -1;
            }
            if (j.progressive) {
                if (decode_scan_prog(&j) != 0) {
                    jpeg_clear(&j);
                    return -1;
                }
                continue;
            }
            if (decode_scan(&j) != 0) {
                jpeg_clear(&j);
                return -1;
            }
            break;
        }
        if (payload_of(&j, &data, &len) != 0) {
            jpeg_clear(&j);
            return -1;
        }
        if (m == 0xc0 || m == 0xc2) {
            j.progressive = m == 0xc2;
            if (parse_sof(&j, data, len) != 0 || (j.progressive && alloc_coef(&j) != 0)) {
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
        } else if (m == 0xc1 || m == 0xc3 || (m >= 0xc5 && m <= 0xc7) || (m >= 0xc9 && m <= 0xcf)) {
            jpeg_clear(&j);
            return -1;
        }
    }
    if (j.progressive && paint_coef(&j) != 0) {
        jpeg_clear(&j);
        return -1;
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

static int sample_bits(const unsigned char *row, int x, int depth);
static int scale_depth(int v, int depth);

struct PngView {
    int depth;
    int color;
    int samples;
    int bits;
    int filter_bpp;
    int have_gray;
    int gray_key;
    int have_rgb;
    int key_r;
    int key_g;
    int key_b;
    int npal;
    const unsigned char *pal_r;
    const unsigned char *pal_g;
    const unsigned char *pal_b;
    const unsigned char *pal_a;
};

static int pass_dim(int size, int origin, int step);
static int paint_pass(unsigned char *raw, int pass_w, int pass_h, unsigned char *out, int full_w, int full_h, int x0,
                      int y0, int dx, int dy, const struct PngView *view);

int image_decode(const unsigned char *bytes, int n, unsigned char **rgba, int *w, int *h) {
    static const unsigned char sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    unsigned char *idat = NULL;
    size_t idat_n = 0;
    size_t idat_cap = 0;
    unsigned long width = 0;
    unsigned long height = 0;
    int depth = 0;
    int color = 0;
    int samples = 0;
    int bits = 0;
    int filter_bpp = 0;
    int stride = 0;
    int at = 8;
    int saw = 0;
    int npal = 0;
    int have_gray = 0;
    int gray_key = 0;
    int have_rgb = 0;
    int key_r = 0;
    int key_g = 0;
    int key_b = 0;
    int interlace = 0;
    unsigned char pal_r[256];
    unsigned char pal_g[256];
    unsigned char pal_b[256];
    unsigned char pal_a[256];
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
            if (data[10] != 0 || data[11] != 0 || data[12] > 1 || width == 0 || height == 0 || width > 4096 ||
                height > 4096) {
                free(idat);
                return -1;
            }
            interlace = data[12];
            saw = 1;
        } else if (memcmp(type, "PLTE", 4) == 0) {
            int i = 0;
            if (!saw || npal != 0 || (color != 2 && color != 3 && color != 6) || len == 0 || len > 768 ||
                len % 3 != 0) {
                free(idat);
                return -1;
            }
            if (color == 3) {
                npal = (int)(len / 3);
                for (i = 0; i < npal; i++) {
                    pal_r[i] = data[i * 3];
                    pal_g[i] = data[i * 3 + 1];
                    pal_b[i] = data[i * 3 + 2];
                    pal_a[i] = 255;
                }
            }
        } else if (memcmp(type, "tRNS", 4) == 0) {
            unsigned long i = 0;
            if (!saw) {
                free(idat);
                return -1;
            }
            if (color == 3) {
                if (npal == 0 || len > (unsigned long)npal) {
                    free(idat);
                    return -1;
                }
                for (i = 0; i < len; i++) {
                    pal_a[i] = data[i];
                }
            } else if (color == 0) {
                int key = 0;
                if (have_gray || len < 2) {
                    free(idat);
                    return -1;
                }
                key = (data[0] << 8) | data[1];
                if (depth == 16 || (depth >= 1 && depth <= 8 && key < (1 << depth))) {
                    gray_key = key;
                    have_gray = 1;
                }
            } else if (color == 2) {
                if (have_rgb || len < 6) {
                    free(idat);
                    return -1;
                }
                key_r = (data[0] << 8) | data[1];
                key_g = (data[2] << 8) | data[3];
                key_b = (data[4] << 8) | data[5];
                if (depth == 16 || (key_r < 256 && key_g < 256 && key_b < 256)) {
                    have_rgb = 1;
                }
            } else {
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
    if (color == 0 || color == 3) {
        samples = 1;
    } else if (color == 2) {
        samples = 3;
    } else if (color == 4) {
        samples = 2;
    } else if (color == 6) {
        samples = 4;
    }
    if (samples == 0 || idat == NULL || width == 0 || height == 0 || (color == 3 && npal < 1)) {
        free(idat);
        return -1;
    }
    if (color == 3) {
        if (depth != 1 && depth != 2 && depth != 4 && depth != 8) {
            free(idat);
            return -1;
        }
    } else if (color == 0) {
        if (depth != 1 && depth != 2 && depth != 4 && depth != 8 && depth != 16) {
            free(idat);
            return -1;
        }
    } else if (depth != 8 && depth != 16) {
        free(idat);
        return -1;
    }
    bits = depth * samples;
    filter_bpp = (bits + 7) / 8;
    if (filter_bpp < 1 || bits < 1 || width > (1u << 20) / (unsigned)bits) {
        free(idat);
        return -1;
    }
    if (interlace == 0) {
        stride = ((int)width * bits + 7) / 8;
        if (stride < 1 || height > (1u << 28) / (unsigned)(stride + 1)) {
            free(idat);
            return -1;
        }
        raw_n = (uLongf)((stride + 1) * (int)height);
    } else {
        static const int adam_x[7] = {0, 4, 0, 2, 0, 1, 0};
        static const int adam_y[7] = {0, 0, 4, 0, 2, 0, 1};
        static const int adam_dx[7] = {8, 8, 4, 4, 2, 2, 1};
        static const int adam_dy[7] = {8, 8, 8, 4, 4, 2, 2};
        int pass = 0;
        raw_n = 0;
        for (pass = 0; pass < 7; pass++) {
            int pw = pass_dim((int)width, adam_x[pass], adam_dx[pass]);
            int ph = pass_dim((int)height, adam_y[pass], adam_dy[pass]);
            int row_bytes = 0;
            if (pw < 1 || ph < 1) {
                continue;
            }
            if (pw > (1 << 20) / bits) {
                free(idat);
                return -1;
            }
            row_bytes = (pw * bits + 7) / 8;
            if (row_bytes < 1 || ph > (1 << 28) / (row_bytes + 1)) {
                free(idat);
                return -1;
            }
            raw_n += (uLongf)(row_bytes + 1) * (uLongf)ph;
        }
    }
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
    if (inflate(&z, Z_FINISH) != Z_STREAM_END || z.total_out != raw_n) {
        inflateEnd(&z);
        free(idat);
        free(raw);
        return -1;
    }
    inflateEnd(&z);
    free(idat);
    idat = NULL;
    out = calloc((size_t)width * (size_t)height, 4);
    if (out == NULL) {
        free(raw);
        return -1;
    }
    {
        struct PngView view;
        unsigned char *cursor = raw;
        size_t used = 0;
        int pass = 0;
        static const int adam_x[7] = {0, 4, 0, 2, 0, 1, 0};
        static const int adam_y[7] = {0, 0, 4, 0, 2, 0, 1};
        static const int adam_dx[7] = {8, 8, 4, 4, 2, 2, 1};
        static const int adam_dy[7] = {8, 8, 8, 4, 4, 2, 2};
        int passes = interlace ? 7 : 1;
        memset(&view, 0, sizeof view);
        view.depth = depth;
        view.color = color;
        view.samples = samples;
        view.bits = bits;
        view.filter_bpp = filter_bpp;
        view.have_gray = have_gray;
        view.gray_key = gray_key;
        view.have_rgb = have_rgb;
        view.key_r = key_r;
        view.key_g = key_g;
        view.key_b = key_b;
        view.npal = npal;
        view.pal_r = pal_r;
        view.pal_g = pal_g;
        view.pal_b = pal_b;
        view.pal_a = pal_a;
        for (pass = 0; pass < passes; pass++) {
            int x0 = interlace ? adam_x[pass] : 0;
            int y0 = interlace ? adam_y[pass] : 0;
            int dx = interlace ? adam_dx[pass] : 1;
            int dy = interlace ? adam_dy[pass] : 1;
            int pw = interlace ? pass_dim((int)width, x0, dx) : (int)width;
            int ph = interlace ? pass_dim((int)height, y0, dy) : (int)height;
            int got = paint_pass(cursor, pw, ph, out, (int)width, (int)height, x0, y0, dx, dy, &view);
            if (got < 0 || used > (size_t)raw_n - (size_t)got) {
                free(out);
                free(raw);
                return -1;
            }
            cursor += got;
            used += (size_t)got;
        }
        if (used != (size_t)raw_n) {
            free(out);
            free(raw);
            return -1;
        }
    }
    free(raw);
    *rgba = out;
    *w = (int)width;
    *h = (int)height;
    return 0;
}

static int sample_bits(const unsigned char *row, int x, int depth) {
    int bit = x * depth;
    int shift = 7 - (bit & 7) - (depth - 1);
    if (row == NULL || x < 0 || depth < 1 || depth > 8 || shift < 0) {
        return 0;
    }
    return (row[bit >> 3] >> shift) & ((1 << depth) - 1);
}

static int scale_depth(int v, int depth) {
    int maxv = (1 << depth) - 1;
    if (depth >= 8 || maxv <= 0) {
        return v;
    }
    return v * 255 / maxv;
}

static int pass_dim(int size, int origin, int step) {
    if (size <= origin || step < 1) {
        return 0;
    }
    return (size - origin + step - 1) / step;
}

/* Unfilter one Adam7 pass, or the whole image, and write RGBA. Returns bytes consumed, or -1. */
static int paint_pass(unsigned char *raw, int pass_w, int pass_h, unsigned char *out, int full_w, int full_h, int x0,
                      int y0, int dx, int dy, const struct PngView *view) {
    int stride = 0;
    int y = 0;
    int x = 0;
    if (pass_w < 1 || pass_h < 1) {
        return 0;
    }
    if (view == NULL || view->bits < 1 || pass_w > (1 << 20) / view->bits) {
        return -1;
    }
    stride = (pass_w * view->bits + 7) / 8;
    if (stride < 1 || view->filter_bpp < 1 || view->filter_bpp > stride || pass_h > (1 << 28) / (stride + 1)) {
        return -1;
    }
    if (unfilter(raw, pass_h, stride, view->filter_bpp) != 0) {
        return -1;
    }
    for (y = 0; y < pass_h; y++) {
        unsigned char *pix = raw + (size_t)y * (size_t)(stride + 1) + 1;
        for (x = 0; x < pass_w; x++) {
            int r = 0;
            int g = 0;
            int b = 0;
            int a = 255;
            int dest_x = x0 + x * dx;
            int dest_y = y0 + y * dy;
            unsigned char *d = NULL;
            if (dest_x < 0 || dest_y < 0 || dest_x >= full_w || dest_y >= full_h) {
                return -1;
            }
            if (view->depth == 16 && view->color == 0) {
                int s = (pix[x * 2] << 8) | pix[x * 2 + 1];
                g = pix[x * 2];
                r = g;
                b = g;
                if (view->have_gray && s == view->gray_key) {
                    a = 0;
                }
            } else if (view->depth == 16 && view->color == 4) {
                g = pix[x * 4];
                a = pix[x * 4 + 2];
                r = g;
                b = g;
            } else if (view->depth == 16 && view->color == 2) {
                int sr = (pix[x * 6] << 8) | pix[x * 6 + 1];
                int sg = (pix[x * 6 + 2] << 8) | pix[x * 6 + 3];
                int sb = (pix[x * 6 + 4] << 8) | pix[x * 6 + 5];
                r = pix[x * 6];
                g = pix[x * 6 + 2];
                b = pix[x * 6 + 4];
                if (view->have_rgb && sr == view->key_r && sg == view->key_g && sb == view->key_b) {
                    a = 0;
                }
            } else if (view->depth == 16 && view->color == 6) {
                r = pix[x * 8];
                g = pix[x * 8 + 2];
                b = pix[x * 8 + 4];
                a = pix[x * 8 + 6];
            } else if (view->color == 0) {
                int s = sample_bits(pix, x, view->depth);
                if (view->have_gray && s == view->gray_key) {
                    a = 0;
                }
                g = scale_depth(s, view->depth);
                r = g;
                b = g;
            } else if (view->color == 4) {
                g = pix[x * 2];
                a = pix[x * 2 + 1];
                r = g;
                b = g;
            } else if (view->color == 3) {
                int idx = sample_bits(pix, x, view->depth);
                if (idx < 0 || idx >= view->npal) {
                    return -1;
                }
                r = view->pal_r[idx];
                g = view->pal_g[idx];
                b = view->pal_b[idx];
                a = view->pal_a[idx];
            } else if (view->color == 2) {
                unsigned char *s = pix + x * 3;
                r = s[0];
                g = s[1];
                b = s[2];
                if (view->have_rgb && r == view->key_r && g == view->key_g && b == view->key_b) {
                    a = 0;
                }
            } else {
                unsigned char *s = pix + x * 4;
                r = s[0];
                g = s[1];
                b = s[2];
                a = s[3];
            }
            d = out + ((size_t)dest_y * (size_t)full_w + (size_t)dest_x) * 4;
            d[0] = (unsigned char)r;
            d[1] = (unsigned char)g;
            d[2] = (unsigned char)b;
            d[3] = (unsigned char)a;
        }
    }
    return (stride + 1) * pass_h;
}
