#ifndef PENSHIP_CHROME_H
#define PENSHIP_CHROME_H

enum {
    CHROME_OPEN = 0,
    CHROME_SAVE = 1,
    CHROME_SAVE_AS = 2,
    CHROME_EXPORT = 3,
    CHROME_CONTENTS = 4,
    CHROME_SMALLER = 5,
    CHROME_LARGER = 6,
    CHROME_FONT = 7,
    CHROME_TYPEWRITER = 8,
    CHROME_FOCUS = 9,
    CHROME_LINE = 10,
    CHROME_THEME = 11,
    CHROME_SOURCE = 12,
    CHROME_SETTINGS = 13,
    CHROME_DONE = 14,
    CHROME_BOLD = 15,
    CHROME_ITALIC = 16,
    CHROME_STRIKE = 17,
    CHROME_CODE = 18,
    CHROME_LINK = 19,
    CHROME_HEAD = 20,
    CHROME_LIST = 21,
    CHROME_TASK = 22,
    CHROME_QUOTE = 23,
    CHROME_RULE = 24,
    CHROME_TABLE = 25,
    CHROME_BLOCK = 26,
    CHROME_READONLY = 27,
    CHROME_FIND = 28,
    CHROME_OPTIONS = 29,
    CHROME_FONTFILE = 30,
    CHROME_GLIDE = 31,
    CHROME_SECTIONS = 32,
    CHROME_COUNT = 33
};

struct Chrome {
    int vertical;
    int text_x, text_y, text_w, text_h;
    int x[CHROME_COUNT];
    int y[CHROME_COUNT];
    int w[CHROME_COUNT];
    int h[CHROME_COUNT];
};

/* Place the writing surface, the file menu, or the settings screen.
   screen 0 is the page and the format bar. 1 is Open, Save, and the other file actions. 2 is the settings list.
   scale_milli is the system font scale times 1000. scroll is the list offset in pixels.
   bar_scroll slides the formatting row. Either pointer may be null.
   The row is placed only while editing is non-zero, and it stays inside the text viewport. */
void chrome_place(struct Chrome *chrome, int width, int height, int inset_top, int inset_bottom, int density,
                  int screen, int scale_milli, int *scroll, int *bar_scroll, int editing);

/* Menu label size in pixels. 24sp at scale 1000, then the system font scale. */
int chrome_label_px(int density, int scale_milli);

/* Formatting-bar label size. 18sp, then the system font scale, so four words fit one row. */
int chrome_bar_px(int density, int scale_milli);

/* Height of one menu or contents row. At least a 48dp thumb, and tall enough for the label. */
int chrome_row_px(int density, int scale_milli);

/* Place n rows of row_h inside [top, bottom). ys and hs must hold n entries. n above 256 is refused.
   scroll is clamped and may be null. Returns 0, or -1. */
int chrome_stack(int *ys, int *hs, int n, int top, int bottom, int row_h, int *scroll);

/* Index of the control under the point, or -1. */
int chrome_hit(const struct Chrome *chrome, int x, int y);

const char *chrome_label(int index);

/* Document text size in pixels for a density and a step from 0 through 6. */
int text_px(int density, int step);
int text_step_count(void);
int text_step_default(void);

#endif
