#include "chrome.h"

#include <limits.h>
#include <stddef.h>

static const char *k_labels[CHROME_COUNT] = {
    "Open",   "Save",     "Save as", "Export", "Contents", "Smaller", "Larger", "Font", "Typewriter",
    "Focus",  "Line",     "Theme",   "Source", "Menu",     "Done",    "Bold",   "Italic", "Strike", "Code",
    "Link",   "Head",     "List",    "Task",   "Quote",    "Rule",    "Table",  "Block",  "Read only", "Find",
    "Settings", "Font file", "Glide", "Sections", "Bar", "Download faces", "Font lists"};
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
    return 4;
}

static int scaled_px(int density, int step, int scale_milli) {
    if (scale_milli < 500 || scale_milli > 3000) {
        scale_milli = 1000;
    }
    int px = text_px(density, step);
    if (px > 0 && scale_milli > INT_MAX / px) {
        return px;
    }
    px = px * scale_milli / 1000;
    if (px < 8) {
        px = 8;
    }
    return px;
}

int chrome_label_px(int density, int scale_milli) {
    return scaled_px(density, text_step_default(), scale_milli);
}

int chrome_bar_px(int density, int scale_milli) {
    return scaled_px(density, 2, scale_milli);
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

int chrome_row_px(int density, int scale_milli) {
    int row = thumb_px(density);
    int label = chrome_label_px(density, scale_milli);
    int padded = label + label / 2;
    if (padded > row) {
        row = padded;
    }
    return row;
}

static void place_column(struct Chrome *chrome, const int *rows, int n, int width, int inset_top, int inner, int thumb,
                         int row, int *scroll) {
    int total = row > 0 && n > INT_MAX / row ? INT_MAX : row * n;
    int max_scroll = total > inner ? total - inner : 0;
    int at = 0;
    if (scroll != NULL) {
        at = *scroll;
        if (at < 0) {
            at = 0;
        }
        if (at > max_scroll) {
            at = max_scroll;
        }
        *scroll = at;
    }
    int span = width;
    int max_span = thumb * 10;
    if (max_span > 0 && span > max_span) {
        span = max_span;
    }
    int x0 = (width - span) / 2;
    for (int i = 0; i < n; i++) {
        place_one(chrome, rows[i], x0, inset_top + i * row - at, span, row);
    }
}

/* Settings is grouped. Each group leaves a band above its first row for a title. */
static void place_settings(struct Chrome *chrome, int width, int inset_top, int inner, int thumb, int row, int *scroll) {
    static const int type_rows[] = {CHROME_SMALLER, CHROME_LARGER, CHROME_FONT};
    static const int face_rows[] = {CHROME_FONTFILE, CHROME_GETFACES, CHROME_LISTS};
    static const int read_rows[] = {CHROME_TYPEWRITER, CHROME_FOCUS, CHROME_LINE, CHROME_SECTIONS, CHROME_BAR, CHROME_GLIDE};
    static const int page_rows[] = {CHROME_THEME, CHROME_SOURCE, CHROME_READONLY};
    static const int *groups[] = {type_rows, face_rows, read_rows, page_rows};
    static const int counts[] = {3, 3, 6, 3};
    int head = row / 2;
    int total = 0;
    int max_scroll = 0;
    int at = 0;
    int span = width;
    int max_span = thumb * 10;
    int x0 = 0;
    int y = 0;
    if (head < 28) {
        head = 28;
    }
    for (int g = 0; g < 4; g++) {
        total += head + counts[g] * row;
    }
    max_scroll = total > inner ? total - inner : 0;
    if (scroll != NULL) {
        at = *scroll;
        if (at < 0) {
            at = 0;
        }
        if (at > max_scroll) {
            at = max_scroll;
        }
        *scroll = at;
    }
    if (max_span > 0 && span > max_span) {
        span = max_span;
    }
    x0 = (width - span) / 2;
    y = inset_top - at;
    chrome->head_n = 4;
    chrome->head_x = x0;
    chrome->head_w = span;
    chrome->head_h = head;
    for (int g = 0; g < 4; g++) {
        chrome->head_y[g] = y;
        y += head;
        for (int i = 0; i < counts[g]; i++) {
            place_one(chrome, groups[g][i], x0, y, span, row);
            y += row;
        }
    }
}

/* The menu mark floats at the end, under the status inset. The keyboard covers the bottom. */
static void place_mark(struct Chrome *chrome, int width, int height, int inset_top, int inset_bottom, int inner,
                       int thumb) {
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
}

void chrome_place(struct Chrome *chrome, int width, int height, int inset_top, int inset_bottom, int density,
                  int screen, int scale_milli, int *scroll, int *bar_scroll, int editing) {
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
    chrome->head_n = 0;
    chrome->vertical = 0;
    int thumb = thumb_px(density);
    int inner = height - inset_top - inset_bottom;
    if (inner < 1) {
        inner = 1;
    }
    if (screen == 1 || screen == 2) {
        static const int k_menu[] = {CHROME_OPEN,   CHROME_SAVE,     CHROME_SAVE_AS, CHROME_EXPORT,
                                    CHROME_CONTENTS, CHROME_FIND,    CHROME_OPTIONS};
        const int *rows = k_menu;
        int n = (int)(sizeof k_menu / sizeof k_menu[0]);
        place_mark(chrome, width, height, inset_top, inset_bottom, inner, thumb);
        /* The mark floats on the first rows. The list is not pushed down to clear it. */
        int row = chrome_row_px(density, scale_milli);
        if (screen == 2) {
            place_settings(chrome, width, inset_top, inner, thumb, row, scroll);
        } else {
            place_column(chrome, rows, n, width, inset_top, inner, thumb, row, scroll);
        }
        chrome->text_x = 0;
        chrome->text_y = 0;
        chrome->text_w = 1;
        chrome->text_h = 1;
        return;
    }
    place_mark(chrome, width, height, inset_top, inset_bottom, inner, thumb);
    int text_h = inner;
    if (editing && width >= 4 && inner > thumb * 2) {
        static const int k_bar[] = {CHROME_BOLD,  CHROME_ITALIC, CHROME_STRIKE, CHROME_CODE, CHROME_LINK, CHROME_HEAD,
                                    CHROME_LIST,  CHROME_TASK,   CHROME_QUOTE,  CHROME_RULE, CHROME_TABLE, CHROME_BLOCK};
        int nbar = (int)(sizeof k_bar / sizeof k_bar[0]);
        int col = thumb;
        int share = nbar > 0 ? width / nbar : width;
        int bar_h = thumb;
        if (col > width) {
            col = width;
        }
        if (col < 1) {
            col = 1;
        }
        /* The strip stays a thumb tall, like a format bar. The slots share the width. */
        int by = height - inset_bottom - bar_h;
        if (by < inset_top) {
            by = inset_top;
        }
        if (nbar > 0 && share >= 44) {
            int base = width / nbar;
            int rem = width % nbar;
            int x = 0;
            if (bar_scroll != NULL) {
                *bar_scroll = 0;
            }
            for (int i = 0; i < nbar; i++) {
                int cw = base + (i < rem ? 1 : 0);
                if (cw < 1) {
                    cw = 1;
                }
                place_one(chrome, k_bar[i], x, by, cw, bar_h);
                x += cw;
            }
        } else {
            int total = col > INT_MAX / nbar ? INT_MAX : col * nbar;
            int max_scroll = total > width ? total - width : 0;
            int at = 0;
            if (bar_scroll != NULL) {
                at = *bar_scroll;
                if (at < 0) {
                    at = 0;
                }
                if (at > max_scroll) {
                    at = max_scroll;
                }
                *bar_scroll = at;
            }
            for (int i = 0; i < nbar; i++) {
                place_one(chrome, k_bar[i], i * col - at, by, col, bar_h);
            }
        }
        int air = thumb / 8;
        if (air < 8) {
            air = 8;
        }
        text_h = inner - bar_h - air;
        if (text_h < 1) {
            text_h = 1;
        }
    }
    int body = text_px(density, text_step_default());
    int measure = body > INT_MAX / 30 ? width : body * 30;
    if (measure > width) {
        measure = width;
    }
    if (measure < 1) {
        measure = 1;
    }
    chrome->text_x = (width - measure) / 2;
    chrome->text_y = inset_top;
    chrome->text_w = measure;
    chrome->text_h = text_h;
}

int chrome_stack(int *ys, int *hs, int n, int top, int bottom, int row_h, int *scroll) {
    if (ys == NULL || hs == NULL || n < 0 || n > 256) {
        return -1;
    }
    if (n == 0) {
        return 0;
    }
    if (row_h < 1) {
        row_h = 1;
    }
    if (row_h > 4096) {
        row_h = 4096;
    }
    if (bottom < top) {
        bottom = top;
    }
    int inner = bottom - top;
    if (row_h > 0 && n > INT_MAX / row_h) {
        return -1;
    }
    int total = row_h * n;
    int max_scroll = total > inner ? total - inner : 0;
    int at = 0;
    if (scroll != NULL) {
        at = *scroll;
        if (at < 0) {
            at = 0;
        }
        if (at > max_scroll) {
            at = max_scroll;
        }
        *scroll = at;
    }
    for (int i = 0; i < n; i++) {
        ys[i] = top + i * row_h - at;
        hs[i] = row_h;
    }
    return 0;
}

int chrome_hit(const struct Chrome *chrome, int x, int y) {
    if (chrome == NULL) {
        return -1;
    }
    /* The mark floats on top of the column and the first rows. It wins the tap. */
    if (chrome->w[CHROME_SETTINGS] > 0 && chrome->h[CHROME_SETTINGS] > 0 && x >= chrome->x[CHROME_SETTINGS] &&
        y >= chrome->y[CHROME_SETTINGS] && x < chrome->x[CHROME_SETTINGS] + chrome->w[CHROME_SETTINGS] &&
        y < chrome->y[CHROME_SETTINGS] + chrome->h[CHROME_SETTINGS]) {
        return CHROME_SETTINGS;
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
