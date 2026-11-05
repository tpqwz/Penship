#include "chrome.h"

#include <limits.h>
#include <stddef.h>

static const char *k_labels[CHROME_COUNT] = {"Open",     "Save",    "Smaller", "Larger", "Font", "Typewriter",
                                             "Focus",    "Theme",   "Source",  "Menu",   "Done"};
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

static void hide_controls(struct Chrome *chrome) {
    for (int i = 0; i < CHROME_COUNT; i++) {
        chrome->x[i] = 0;
        chrome->y[i] = 0;
        chrome->w[i] = 0;
        chrome->h[i] = 0;
    }
}

static void place_one(struct Chrome *chrome, int index, int x, int y, int w, int h) {
    if (w < 1) {
        w = 1;
    }
    if (h < 1) {
        h = 1;
    }
    chrome->x[index] = x;
    chrome->y[index] = y;
    chrome->w[index] = w;
    chrome->h[index] = h;
}

void chrome_place(struct Chrome *chrome, int width, int height, int inset_top, int inset_bottom, int density,
                  int settings) {
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
    hide_controls(chrome);
    chrome->vertical = 0;
    int thumb = thumb_px(density);
    int inner = height - inset_top - inset_bottom;
    if (inner < 1) {
        inner = 1;
    }
    if (settings) {
        static const int k_rows[] = {CHROME_OPEN,  CHROME_SAVE,     CHROME_SMALLER, CHROME_LARGER, CHROME_FONT,
                                    CHROME_TYPEWRITER, CHROME_FOCUS, CHROME_THEME,  CHROME_SOURCE, CHROME_DONE};
        int n = (int)(sizeof k_rows / sizeof k_rows[0]);
        int item_h = inner / n;
        if (item_h < 1) {
            item_h = 1;
        }
        for (int i = 0; i < n; i++) {
            int h = (i == n - 1) ? inner - item_h * (n - 1) : item_h;
            if (h < 1) {
                h = 1;
            }
            place_one(chrome, k_rows[i], 0, inset_top + i * item_h, width, h);
        }
        chrome->text_x = 0;
        chrome->text_y = 0;
        chrome->text_w = 1;
        chrome->text_h = 1;
        return;
    }
    /* The menu mark floats at the end, under the status inset. The keyboard covers the bottom. */
    int side = thumb;
    if (side > width) {
        side = width;
    }
    if (side > inner) {
        side = inner;
    }
    if (side < 1) {
        side = 1;
    }
    int gap = side / 5;
    int x = width - side - gap;
    if (x < 0) {
        x = 0;
    }
    int y = inset_top + gap;
    if (y + side > height - inset_bottom && inset_top <= height - inset_bottom - side) {
        y = height - inset_bottom - side;
    }
    if (y < inset_top) {
        y = inset_top;
    }
    place_one(chrome, CHROME_SETTINGS, x, y, side, side);
    chrome->text_x = 0;
    chrome->text_y = inset_top;
    chrome->text_w = width;
    chrome->text_h = inner;
}

int chrome_hit(const struct Chrome *chrome, int x, int y) {
    if (chrome == NULL) {
        return -1;
    }
    for (int i = 0; i < CHROME_COUNT; i++) {
        if (chrome->w[i] <= 0 || chrome->h[i] <= 0) {
            continue;
        }
        if (x >= chrome->x[i] && y >= chrome->y[i] && x < chrome->x[i] + chrome->w[i] &&
            y < chrome->y[i] + chrome->h[i]) {
            return i;
        }
    }
    return -1;
}
