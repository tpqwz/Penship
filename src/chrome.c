#include "chrome.h"

#include <limits.h>
#include <stddef.h>

static const char *k_labels[CHROME_COUNT] = {"Smaller", "Larger", "Font", "Typewriter", "Focus", "Theme",
                                             "Source"};
static const int k_steps[] = {14, 16, 18, 20, 24, 28, 32};

const char *chrome_label(int index) {
    if (index < 0 || index >= CHROME_COUNT) {
        return "";
    }
    return k_labels[index];
}

int text_step_count(void) {
    return (int)(sizeof k_steps / sizeof k_steps[0]);
}

int text_step_default(void) {
    return 2;
}

int text_px(int density, int step) {
    if (density < 120) {
        density = 160;
    }
    if (density > 1000) {
        density = 1000;
    }
    int last = text_step_count() - 1;
    if (step < 0) {
        step = 0;
    }
    if (step > last) {
        step = last;
    }
    int sp = k_steps[step];
    if (sp > 0 && density > INT_MAX / sp) {
        return sp;
    }
    int px = density * sp / 160;
    if (px < 8) {
        px = 8;
    }
    return px;
}

static int thumb_px(int density) {
    if (density < 120) {
        density = 160;
    }
    int thumb = density * 48 / 160;
    if (thumb < 48) {
        thumb = 48;
    }
    return thumb;
}

void chrome_place(struct Chrome *chrome, int width, int height, int inset_top, int inset_bottom, int density) {
    if (width < 1) {
        width = 1;
    }
    if (height < 1) {
        height = 1;
    }
    if (inset_top < 0) {
        inset_top = 0;
    }
    if (inset_bottom < 0) {
        inset_bottom = 0;
    }
    if (inset_top > height) {
        inset_top = height;
    }
    if (inset_bottom > height - inset_top) {
        inset_bottom = height - inset_top;
    }
    int thumb = thumb_px(density);
    int vertical = width >= height && width >= thumb * 8;
    chrome->vertical = vertical;
    if (vertical) {
        int label_px = text_px(density, 0);
        int col_w = label_px * 8;
        if (col_w < thumb * 2) {
            col_w = thumb * 2;
        }
        if (col_w > width / 3) {
            col_w = width / 3;
        }
        if (col_w < 1) {
            col_w = 1;
        }
        int inner = height - inset_top - inset_bottom;
        if (inner < 1) {
            inner = 1;
        }
        int item_h = inner / CHROME_COUNT;
        if (item_h < 1) {
            item_h = 1;
        }
        for (int i = 0; i < CHROME_COUNT; i++) {
            chrome->x[i] = 0;
            chrome->y[i] = inset_top + i * item_h;
            chrome->w[i] = col_w;
            chrome->h[i] = item_h;
        }
        chrome->text_x = col_w;
        chrome->text_y = inset_top;
        chrome->text_w = width - col_w;
        if (chrome->text_w < 1) {
            chrome->text_w = 1;
        }
        chrome->text_h = inner;
        return;
    }
    /* A floating keyboard toolbar sits on the bottom start. Keep the words clear of it. */
    int gutter = density * 88 / 160;
    if (gutter < 0) {
        gutter = 0;
    }
    if (gutter > width / 3) {
        gutter = width / 3;
    }
    int cols = 3;
    int rows = (CHROME_COUNT + cols - 1) / cols;
    int item_w = (width - gutter) / cols;
    if (item_w < 1) {
        item_w = 1;
    }
    int item_h = thumb;
    int bar_h = item_h * rows;
    int y0 = height - inset_bottom - bar_h;
    if (y0 < inset_top) {
        y0 = inset_top;
        int room = height - inset_bottom - y0;
        if (room < rows) {
            room = rows;
        }
        item_h = room / rows;
        if (item_h < 1) {
            item_h = 1;
        }
        bar_h = item_h * rows;
    }
    for (int i = 0; i < CHROME_COUNT; i++) {
        int col = i % cols;
        chrome->x[i] = gutter + col * item_w;
        chrome->y[i] = y0 + (i / cols) * item_h;
        chrome->w[i] = (col == cols - 1) ? width - gutter - (cols - 1) * item_w : item_w;
        chrome->h[i] = item_h;
    }
    chrome->text_x = 0;
    chrome->text_y = inset_top;
    chrome->text_w = width;
    chrome->text_h = y0 - inset_top;
    if (chrome->text_h < 1) {
        chrome->text_h = 1;
    }
}

int chrome_hit(const struct Chrome *chrome, int x, int y) {
    if (chrome == NULL) {
        return -1;
    }
    for (int i = 0; i < CHROME_COUNT; i++) {
        if (x >= chrome->x[i] && y >= chrome->y[i] && x < chrome->x[i] + chrome->w[i] &&
            y < chrome->y[i] + chrome->h[i]) {
            return i;
        }
    }
    return -1;
}
