#include "mdmath.h"

#include <stdio.h>
#include <string.h>

static int fail;

static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "fail: %s\n", msg);
        fail = 1;
    }
}

static int advance(void *user, unsigned cp, int px) {
    (void)user;
    (void)cp;
    return px / 2 > 0 ? px / 2 : 1;
}

static int ascent(void *user, int px) {
    (void)user;
    return px * 3 / 4 > 0 ? px * 3 / 4 : 1;
}

static int descent(void *user, int px) {
    (void)user;
    return px / 4;
}

static void face_of(struct MathFace *face) {
    face->advance = advance;
    face->ascent = ascent;
    face->descent = descent;
    face->user = NULL;
}

static void box(const char *s, int *w, int *a, int *d) {
    struct MathFace face;
    face_of(&face);
    expect(math_measure(s, strlen(s), &face, 40, w, a, d) == 0, s);
}

int main(void) {
    struct MathFace face;
    int w = 0;
    int a = 0;
    int d = 0;
    int xw = 0;
    int xa = 0;
    int sw = 0;
    int sa = 0;
    int sd = 0;
    int fw = 0;
    int fa = 0;
    int rw = 0;
    char ml[512];
    face_of(&face);
    box("x", &xw, &xa, &d);
    expect(xw == 20, "a letter is half the body");
    box("x^2", &sw, &sa, &sd);
    expect(sw > xw && sa > xa, "a superscript is taller and wider");
    box("a_i", &w, &a, &d);
    expect(d > 10, "a subscript hangs below the baseline");
    box("\\frac{ab}{c}", &fw, &fa, &d);
    expect(fw >= 40 && fa > xa, "a fraction is wider than one letter and taller");
    box("\\sqrt{x}", &rw, &a, &d);
    expect(rw > xw, "a root is wider than its body");
    box("\\sqrt[3]{x}", &w, &a, &d);
    expect(w > rw, "a degree widens the root");
    box("\\frac{\\sqrt{a^2+b^2}}{c}", &w, &a, &d);
    expect(a > fa, "a root inside a fraction adds height");
    expect(math_measure("\\frac{1}", 8, &face, 40, &w, &a, &d) != 0, "a fraction needs two groups");
    expect(math_measure("", 0, &face, 40, &w, &a, &d) != 0, "an empty formula is refused");
    expect(math_mathml("E=mc^2", 6, 1, ml, sizeof ml) == 0, "mathml");
    expect(strstr(ml, "<msup>") != NULL && strstr(ml, "display=\"block\"") != NULL, "a power is a superscript");
    expect(math_mathml("\\frac{1}{2}", 11, 0, ml, sizeof ml) == 0 && strstr(ml, "<mfrac>") != NULL, "a fraction is mfrac");
    if (fail) {
        return 1;
    }
    puts("math_test ok");
    return 0;
}
