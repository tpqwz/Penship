#ifndef PENSHIP_MATH_H
#define PENSHIP_MATH_H

#include <stddef.h>

/* A TeX subset: groups, ^ and _, \frac, \sqrt, \text, and named symbols.
   A formula longer than the parser's limit, or outside this subset, fails.
   The caller supplies glyph sizes. px is the body size in pixels. */

struct MathFace {
    int (*advance)(void *user, unsigned cp, int px);
    int (*ascent)(void *user, int px);
    int (*descent)(void *user, int px);
    void *user;
};

struct MathDraw {
    void (*glyph)(void *user, unsigned cp, int x, int baseline, int px);
    void (*rule)(void *user, int x, int y, int w, int h);
    void *user;
};

int math_measure(const char *s, size_t n, const struct MathFace *face, int px, int *width, int *ascent, int *descent);

/* origin_x and baseline are the formula's left and baseline, in the same space as the draw callbacks. */
int math_draw(const char *s, size_t n, const struct MathFace *face, int px, int origin_x, int baseline,
              const struct MathDraw *draw);

/* MathML for the same subset. display selects the block form. Returns 0, or -1 if it does not fit. */
int math_mathml(const char *s, size_t n, int display, char *out, size_t cap);

#endif
