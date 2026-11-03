#include "page.h"

#include <stddef.h>

enum {
    GLYPH_W = 5,
    GLYPH_H = 7,
    GLYPH_GAP = 1
};

/* 5x7 lowercase, column-major, bit 0 at the top. */
static const uint8_t GLYPHS[26][GLYPH_W] = {
    /* a */ {0x38, 0x44, 0x54, 0x54, 0x38},
    /* b */ {0x7f, 0x44, 0x44, 0x38, 0x00},
    /* c */ {0x3c, 0x42, 0x42, 0x42, 0x00},
    /* d */ {0x38, 0x44, 0x44, 0x44, 0x7f},
    /* e */ {0x3c, 0x4a, 0x4a, 0x4a, 0x2c},
    /* f */ {0x04, 0x7e, 0x05, 0x05, 0x00},
    /* g */ {0x26, 0x49, 0x49, 0x49, 0x3e},
    /* h */ {0x7f, 0x04, 0x04, 0x78, 0x00},
    /* i */ {0x00, 0x44, 0x7d, 0x40, 0x00},
    /* j */ {0x20, 0x40, 0x44, 0x3d, 0x00},
    /* k */ {0x7f, 0x08, 0x14, 0x22, 0x40},
    /* l */ {0x00, 0x01, 0x7f, 0x40, 0x40},
    /* m */ {0x7c, 0x04, 0x18, 0x04, 0x78},
    /* n */ {0x7c, 0x04, 0x04, 0x78, 0x00},
    /* o */ {0x3c, 0x42, 0x42, 0x42, 0x3c},
    /* p */ {0x7e, 0x12, 0x12, 0x0c, 0x00},
    /* q */ {0x0c, 0x12, 0x12, 0x12, 0x7e},
    /* r */ {0x7c, 0x08, 0x04, 0x04, 0x00},
    /* s */ {0x44, 0x4a, 0x4a, 0x4a, 0x32},
    /* t */ {0x04, 0x04, 0x3f, 0x44, 0x44},
    /* u */ {0x3c, 0x40, 0x40, 0x40, 0x7c},
    /* v */ {0x1c, 0x20, 0x40, 0x20, 0x1c},
    /* w */ {0x3c, 0x40, 0x30, 0x40, 0x3c},
    /* x */ {0x44, 0x28, 0x10, 0x28, 0x44},
    /* y */ {0x26, 0x48, 0x48, 0x48, 0x3e},
    /* z */ {0x44, 0x64, 0x54, 0x4c, 0x44},
};

static const char WORD[] = "penship";

static void fill(uint8_t *px, int count, uint8_t r, uint8_t g, uint8_t b) {
    for (int i = 0; i < count; i++) {
        px[i * 4 + 0] = r;
        px[i * 4 + 1] = g;
        px[i * 4 + 2] = b;
        px[i * 4 + 3] = 255;
    }
}

static void blit(uint8_t *px, int w, int h, int x, int y, int scale, const uint8_t cols[GLYPH_W]) {
    for (int col = 0; col < GLYPH_W; col++) {
        for (int row = 0; row < GLYPH_H; row++) {
            if (((cols[col] >> row) & 1) == 0) {
                continue;
            }
            for (int dy = 0; dy < scale; dy++) {
                int py = y + row * scale + dy;
                if (py < 0 || py >= h) {
                    continue;
                }
                for (int dx = 0; dx < scale; dx++) {
                    int px_x = x + col * scale + dx;
                    if (px_x < 0 || px_x >= w) {
                        continue;
                    }
                    uint8_t *p = px + ((size_t)py * (size_t)w + (size_t)px_x) * 4;
                    p[0] = PAGE_INK_R;
                    p[1] = PAGE_INK_G;
                    p[2] = PAGE_INK_B;
                    p[3] = 255;
                }
            }
        }
    }
}

void page_draw(uint8_t *px, int width, int height) {
    if (width <= 0 || height <= 0) {
        return;
    }
    fill(px, width * height, PAGE_PAPER_R, PAGE_PAPER_G, PAGE_PAPER_B);

    int n = (int)(sizeof WORD - 1);
    int text_w = n * GLYPH_W + (n - 1) * GLYPH_GAP;
    int scale = width / text_w;
    int scale_y = height / GLYPH_H;
    if (scale_y < scale) {
        scale = scale_y;
    }
    if (scale < 1) {
        scale = 1;
    }
    int x = (width - text_w * scale) / 2;
    int y = (height - GLYPH_H * scale) / 2;
    for (int i = 0; i < n; i++) {
        char ch = WORD[i];
        if (ch >= 'a' && ch <= 'z') {
            blit(px, width, height, x, y, scale, GLYPHS[ch - 'a']);
        }
        x += GLYPH_W * scale;
        if (i + 1 < n) {
            x += GLYPH_GAP * scale;
        }
    }
}
