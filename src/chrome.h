#ifndef PENSHIP_CHROME_H
#define PENSHIP_CHROME_H

enum {
    CHROME_SMALLER = 0,
    CHROME_LARGER = 1,
    CHROME_FONT = 2,
    CHROME_TYPEWRITER = 3,
    CHROME_FOCUS = 4,
    CHROME_THEME = 5,
    CHROME_SOURCE = 6,
    CHROME_COUNT = 7
};

struct Chrome {
    int vertical;
    int text_x, text_y, text_w, text_h;
    int x[CHROME_COUNT];
    int y[CHROME_COUNT];
    int w[CHROME_COUNT];
    int h[CHROME_COUNT];
};

/* Place the same controls. A wide window uses a side column. A narrow window uses rows of three at the bottom, above the inset. */
void chrome_place(struct Chrome *chrome, int width, int height, int inset_top, int inset_bottom, int density);

/* Index of the control under the point, or -1. */
int chrome_hit(const struct Chrome *chrome, int x, int y);

const char *chrome_label(int index);

/* Document text size in pixels for a density and a step from 0 through 6. */
int text_px(int density, int step);
int text_step_count(void);
int text_step_default(void);

#endif
