#include "page.h"

#include <stdio.h>
#include <stdlib.h>

static int fail;

static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "fail: %s\n", msg);
        fail = 1;
    }
}

static uint8_t *at(uint8_t *px, int w, int x, int y) {
    return px + ((size_t)y * (size_t)w + (size_t)x) * 4;
}

/* 1x layout of "penship": 7*5 + 6 gaps = 41 by 7, origin (0, 0).
   'p' column 0 is ink on rows 1..6. The gap after 'p' is column 5. */
static void test_one_to_one(void) {
    const int w = 41;
    const int h = 7;
    uint8_t *px = calloc((size_t)w * (size_t)h, 4);
    expect(px != NULL, "alloc");
    if (px == NULL) {
        return;
    }
    page_draw(px, w, h);
    expect(at(px, w, 0, 0)[0] == PAGE_PAPER_R, "corner is paper");
    expect(at(px, w, 0, 1)[0] == PAGE_INK_R, "p stem is ink");
    expect(at(px, w, 0, 1)[1] == PAGE_INK_G && at(px, w, 0, 1)[2] == PAGE_INK_B, "ink is the page color");
    expect(at(px, w, 5, 1)[0] == PAGE_PAPER_R, "gap after p stays paper");
    expect(at(px, w, w - 1, h - 1)[0] == PAGE_PAPER_R, "opposite corner is paper");
    free(px);
}

/* Scale 2 doubles each glyph pixel. Glyph (0, 1) covers buffer pixels (0..1, 2..3). */
static void test_scale(void) {
    const int w = 82;
    const int h = 14;
    uint8_t *px = calloc((size_t)w * (size_t)h, 4);
    expect(px != NULL, "alloc scale");
    if (px == NULL) {
        return;
    }
    page_draw(px, w, h);
    expect(at(px, w, 0, 2)[0] == PAGE_INK_R, "scaled stem");
    expect(at(px, w, 1, 3)[0] == PAGE_INK_R, "scaled stem covers the whole cell");
    expect(at(px, w, 10, 2)[0] == PAGE_PAPER_R, "scaled gap stays paper");
    free(px);
}

int main(void) {
    test_one_to_one();
    test_scale();
    if (fail) {
        return 1;
    }
    puts("page_test ok");
    return 0;
}
