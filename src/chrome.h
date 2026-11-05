#ifndef PENSHIP_CHROME_H
#define PENSHIP_CHROME_H

enum {
    CHROME_OPEN = 0,
    CHROME_SAVE = 1,
    CHROME_SMALLER = 2,
    CHROME_LARGER = 3,
    CHROME_FONT = 4,
    CHROME_TYPEWRITER = 5,
    CHROME_FOCUS = 6,
    CHROME_THEME = 7,
    CHROME_SOURCE = 8,
    CHROME_SETTINGS = 9,
    CHROME_DONE = 10,
    CHROME_COUNT = 11
};

struct Chrome {
    int vertical;
    int text_x, text_y, text_w, text_h;
    int x[CHROME_COUNT];
    int y[CHROME_COUNT];
    int w[CHROME_COUNT];
    int h[CHROME_COUNT];
};

/* Place the writing surface, or the menu when settings is non-zero.
   The writing surface keeps a small menu mark at the end. The menu has Open, Save, the options, and Done. */
void chrome_place(struct Chrome *chrome, int width, int height, int inset_top, int inset_bottom, int density,
                  int settings);

/* Index of the control under the point, or -1. */
int chrome_hit(const struct Chrome *chrome, int x, int y);

const char *chrome_label(int index);

/* Document text size in pixels for a density and a step from 0 through 6. */
int text_px(int density, int step);
int text_step_count(void);
int text_step_default(void);

#endif
