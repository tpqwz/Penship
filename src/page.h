#ifndef PENSHIP_PAGE_H
#define PENSHIP_PAGE_H

#include <stdint.h>

enum {
    PAGE_PAPER_R = 250,
    PAGE_PAPER_G = 247,
    PAGE_PAPER_B = 242,
    PAGE_INK_R = 28,
    PAGE_INK_G = 27,
    PAGE_INK_B = 25
};

/* Fill a tightly packed RGBA8888 buffer, top row first. */
void page_draw(uint8_t *px, int width, int height);

#endif
