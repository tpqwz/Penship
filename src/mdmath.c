#include "mdmath.h"

#include "utf8.h"

#include <string.h>

enum { MATH_NODES = 160, MATH_DEPTH = 24, N_ROW = 1, N_ATOM = 2, N_FRAC = 3, N_SQRT = 4, N_SCRIPT = 5 };

enum { CMD_NONE = 0, CMD_SYMBOL = 1, CMD_FRAC = 2, CMD_SQRT = 3, CMD_TEXT = 4, CMD_SKIP = 5 };

struct Node {
    unsigned char kind;
    unsigned cp;
    int child;
    int next;
    int sup;
    int sub;
    int w;
    int asc;
    int desc;
};

struct Arena {
    struct Node n[MATH_NODES];
    int count;
    int depth;
};

struct Out {
    char *s;
    size_t n;
    size_t cap;
};

static int node_new(struct Arena *a, int kind) {
    struct Node *t = NULL;
    int id = 0;
    if (a->count >= MATH_NODES) {
        return -1;
    }
    id = a->count;
    a->count++;
    t = &a->n[id];
    memset(t, 0, sizeof *t);
    t->kind = (unsigned char)kind;
    t->child = -1;
    t->next = -1;
    t->sup = -1;
    t->sub = -1;
    return id;
}

static int grow(int a, int b) {
    if (a < 0 || b < 0) {
        return 0;
    }
    if (a > 1000000 - b) {
        return 1000000;
    }
    return a + b;
}

static int larger(int a, int b) {
    return a > b ? a : b;
}

static int cmd_span(const char *s, size_t n, size_t i, size_t *end) {
    if (i >= n || s[i] != '\\') {
        return 0;
    }
    i++;
    if (i >= n) {
        return 0;
    }
    if (s[i] == ' ' || s[i] == ',' || s[i] == ';' || s[i] == '!') {
        *end = i + 1;
        return 1;
    }
    if (!((s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= 'a' && s[i] <= 'z'))) {
        return 0;
    }
    while (i < n && ((s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= 'a' && s[i] <= 'z'))) {
        i++;
    }
    *end = i;
    return 1;
}

static int name_is(const char *s, size_t n, const char *lit) {
    size_t k = strlen(lit);
    return k == n && memcmp(s, lit, n) == 0;
}

static int lookup(const char *s, size_t n, unsigned *cp) {
    static const struct {
        const char *name;
        unsigned cp;
    } symbols[] = {
        {"Gamma", 0x0393}, {"Delta", 0x0394}, {"Theta", 0x0398}, {"Lambda", 0x039B}, {"Xi", 0x039E},
        {"Pi", 0x03A0},    {"Sigma", 0x03A3}, {"Phi", 0x03A6},   {"Psi", 0x03A8},    {"Omega", 0x03A9},
        {"alpha", 0x03B1}, {"beta", 0x03B2},  {"gamma", 0x03B3}, {"delta", 0x03B4},  {"epsilon", 0x03B5},
        {"zeta", 0x03B6},  {"eta", 0x03B7},   {"theta", 0x03B8}, {"iota", 0x03B9},   {"kappa", 0x03BA},
        {"lambda", 0x03BB}, {"mu", 0x03BC},   {"nu", 0x03BD},    {"xi", 0x03BE},     {"pi", 0x03C0},
        {"rho", 0x03C1},   {"sigma", 0x03C3}, {"tau", 0x03C4},   {"upsilon", 0x03C5}, {"phi", 0x03C6},
        {"chi", 0x03C7},   {"psi", 0x03C8},   {"omega", 0x03C9}, {"pm", 0x00B1},     {"mp", 0x2213},
        {"times", 0x00D7}, {"cdot", 0x00B7},  {"div", 0x00F7},   {"leq", 0x2264},    {"geq", 0x2265},
        {"neq", 0x2260},   {"approx", 0x2248}, {"equiv", 0x2261}, {"infty", 0x221E}, {"partial", 0x2202},
        {"sum", 0x2211},   {"prod", 0x220F},  {"int", 0x222B},   {"ldots", 0x2026},  {"cdots", 0x22EF},
        {"to", 0x2192},    {"rightarrow", 0x2192}, {"leftarrow", 0x2190},
    };
    size_t i = 0;
    if (n == 1 && (s[0] == ' ' || s[0] == ',' || s[0] == ';' || s[0] == '!')) {
        if (s[0] == '!') {
            return CMD_SKIP;
        }
        *cp = s[0] == ',' ? 0x2009u : s[0] == ';' ? 0x2005u : (unsigned)' ';
        return CMD_SYMBOL;
    }
    if (name_is(s, n, "frac")) {
        return CMD_FRAC;
    }
    if (name_is(s, n, "sqrt")) {
        return CMD_SQRT;
    }
    if (name_is(s, n, "text") || name_is(s, n, "mathrm")) {
        return CMD_TEXT;
    }
    if (name_is(s, n, "left") || name_is(s, n, "right")) {
        return CMD_SKIP;
    }
    if (name_is(s, n, "quad")) {
        *cp = 0x2003;
        return CMD_SYMBOL;
    }
    if (name_is(s, n, "qquad")) {
        *cp = 0x2001;
        return CMD_SYMBOL;
    }
    for (i = 0; i < sizeof symbols / sizeof symbols[0]; i++) {
        if (name_is(s, n, symbols[i].name)) {
            *cp = symbols[i].cp;
            return CMD_SYMBOL;
        }
    }
    return CMD_NONE;
}

static int parse_item(struct Arena *a, const char *s, size_t n, size_t *io);
static int parse_row(struct Arena *a, const char *s, size_t n, size_t *io, char stop);

static int parse_text(struct Arena *a, const char *s, size_t n, size_t *io) {
    int row = 0;
    int prev = -1;
    if (*io >= n || s[*io] != '{') {
        return -1;
    }
    (*io)++;
    row = node_new(a, N_ROW);
    if (row < 0) {
        return -1;
    }
    while (*io < n && s[*io] != '}') {
        unsigned char c = (unsigned char)s[*io];
        int atom = 0;
        if (c < 32 && c != '\t') {
            return -1;
        }
        atom = node_new(a, N_ATOM);
        if (atom < 0) {
            return -1;
        }
        a->n[atom].cp = c == '\t' ? (unsigned)' ' : c;
        if (prev < 0) {
            a->n[row].child = atom;
        } else {
            a->n[prev].next = atom;
        }
        prev = atom;
        (*io)++;
    }
    if (*io >= n || s[*io] != '}') {
        return -1;
    }
    (*io)++;
    if (a->n[row].child < 0) {
        return -1;
    }
    return row;
}

static int take_group(struct Arena *a, const char *s, size_t n, size_t *io) {
    int row = 0;
    if (*io >= n || s[*io] != '{') {
        return -1;
    }
    (*io)++;
    if (a->depth >= MATH_DEPTH) {
        return -1;
    }
    a->depth++;
    row = parse_row(a, s, n, io, '}');
    a->depth--;
    if (row < 0 || *io >= n || s[*io] != '}') {
        return -1;
    }
    (*io)++;
    return row;
}

static int take_scripts(struct Arena *a, const char *s, size_t n, size_t *io, int base) {
    int sup = -1;
    int sub = -1;
    int id = 0;
    if (base < 0) {
        return -1;
    }
    for (;;) {
        if (*io < n && s[*io] == '^' && sup < 0) {
            (*io)++;
            sup = parse_item(a, s, n, io);
            if (sup < 0) {
                return -1;
            }
            continue;
        }
        if (*io < n && s[*io] == '_' && sub < 0) {
            (*io)++;
            sub = parse_item(a, s, n, io);
            if (sub < 0) {
                return -1;
            }
            continue;
        }
        if (*io < n && s[*io] == '\'' && sup < 0) {
            (*io)++;
            sup = node_new(a, N_ATOM);
            if (sup < 0) {
                return -1;
            }
            a->n[sup].cp = 0x2032;
            continue;
        }
        break;
    }
    if (sup < 0 && sub < 0) {
        return base;
    }
    id = node_new(a, N_SCRIPT);
    if (id < 0) {
        return -1;
    }
    a->n[id].child = base;
    a->n[id].sup = sup;
    a->n[id].sub = sub;
    return id;
}

static int parse_cmd(struct Arena *a, const char *s, size_t n, size_t *io) {
    size_t end = 0;
    unsigned cp = 0;
    int kind = 0;
    int id = 0;
    if (!cmd_span(s, n, *io, &end)) {
        return -1;
    }
    kind = lookup(s + *io + 1, end - (*io + 1), &cp);
    *io = end;
    if (kind == CMD_NONE) {
        return -1;
    }
    if (kind == CMD_SKIP) {
        id = node_new(a, N_ATOM);
        if (id < 0) {
            return -1;
        }
        a->n[id].cp = 0;
        return id;
    }
    if (kind == CMD_SYMBOL) {
        id = node_new(a, N_ATOM);
        if (id < 0) {
            return -1;
        }
        a->n[id].cp = cp;
        return id;
    }
    if (kind == CMD_TEXT) {
        return parse_text(a, s, n, io);
    }
    if (kind == CMD_FRAC) {
        int num = take_group(a, s, n, io);
        int den = 0;
        if (num < 0) {
            return -1;
        }
        den = take_group(a, s, n, io);
        if (den < 0) {
            return -1;
        }
        id = node_new(a, N_FRAC);
        if (id < 0) {
            return -1;
        }
        a->n[id].child = num;
        a->n[id].sub = den;
        return id;
    }
    id = node_new(a, N_SQRT);
    if (id < 0) {
        return -1;
    }
    if (*io < n && s[*io] == '[') {
        int degree = 0;
        (*io)++;
        if (a->depth >= MATH_DEPTH) {
            return -1;
        }
        a->depth++;
        degree = parse_row(a, s, n, io, ']');
        a->depth--;
        if (degree < 0 || *io >= n || s[*io] != ']') {
            return -1;
        }
        (*io)++;
        a->n[id].sup = degree;
    }
    a->n[id].child = take_group(a, s, n, io);
    if (a->n[id].child < 0) {
        return -1;
    }
    return id;
}

static int parse_item(struct Arena *a, const char *s, size_t n, size_t *io) {
    unsigned char c = 0;
    int id = 0;
    if (a->depth >= MATH_DEPTH) {
        return -1;
    }
    while (*io < n && (s[*io] == ' ' || s[*io] == '\t' || s[*io] == '\n')) {
        (*io)++;
    }
    if (*io >= n) {
        return -1;
    }
    c = (unsigned char)s[*io];
    if (c == '{') {
        return take_group(a, s, n, io);
    }
    if (c == '\\') {
        return parse_cmd(a, s, n, io);
    }
    if (c < 33 || c > 126 || c == '}' || c == '^' || c == '_' || c == '$' || c == '&') {
        return -1;
    }
    id = node_new(a, N_ATOM);
    if (id < 0) {
        return -1;
    }
    a->n[id].cp = c;
    (*io)++;
    return id;
}

static int parse_row(struct Arena *a, const char *s, size_t n, size_t *io, char stop) {
    int row = node_new(a, N_ROW);
    int prev = -1;
    if (row < 0) {
        return -1;
    }
    while (*io < n) {
        int item = 0;
        while (*io < n && (s[*io] == ' ' || s[*io] == '\t' || s[*io] == '\n')) {
            (*io)++;
        }
        if (*io >= n || (stop != 0 && s[*io] == stop)) {
            break;
        }
        item = parse_item(a, s, n, io);
        if (item < 0) {
            return -1;
        }
        item = take_scripts(a, s, n, io, item);
        if (item < 0) {
            return -1;
        }
        if (prev < 0) {
            a->n[row].child = item;
        } else {
            a->n[prev].next = item;
        }
        prev = item;
    }
    if (a->n[row].child < 0) {
        return -1;
    }
    return row;
}

static int space_cp(unsigned cp) {
    return cp == 0 || cp == ' ' || cp == 0x2009 || cp == 0x2005 || cp == 0x2003 || cp == 0x2001;
}

static int layout(struct Arena *a, int id, const struct MathFace *face, int px) {
    struct Node *t = NULL;
    int spx = 0;
    if (id < 0 || px < 1 || face == NULL || face->advance == NULL || face->ascent == NULL || face->descent == NULL) {
        return -1;
    }
    t = &a->n[id];
    if (t->kind == N_ATOM) {
        int asc = face->ascent(face->user, px);
        int desc = face->descent(face->user, px);
        if (asc < 1) {
            asc = 1;
        }
        if (desc < 0) {
            desc = 0;
        }
        t->asc = asc;
        t->desc = desc;
        if (t->cp == 0) {
            t->w = 0;
            t->asc = 0;
            t->desc = 0;
        } else if (t->cp == 0x2009) {
            t->w = px / 6;
            if (t->w < 1) {
                t->w = 1;
            }
        } else if (t->cp == 0x2005) {
            t->w = px / 4;
            if (t->w < 1) {
                t->w = 1;
            }
        } else if (t->cp == ' ') {
            t->w = px / 3;
            if (t->w < 1) {
                t->w = 1;
            }
        } else if (t->cp == 0x2003) {
            t->w = px;
        } else if (t->cp == 0x2001) {
            t->w = px > 500000 ? 1000000 : px * 2;
        } else {
            t->w = face->advance(face->user, t->cp, px);
            if (t->w < 1) {
                t->w = 1;
            }
        }
        return 0;
    }
    if (t->kind == N_ROW) {
        int w = 0;
        int asc = 0;
        int desc = 0;
        int c = 0;
        for (c = t->child; c >= 0; c = a->n[c].next) {
            if (layout(a, c, face, px) != 0) {
                return -1;
            }
            w = grow(w, a->n[c].w);
            asc = larger(asc, a->n[c].asc);
            desc = larger(desc, a->n[c].desc);
        }
        t->w = w;
        t->asc = asc < 1 ? face->ascent(face->user, px) : asc;
        t->desc = desc;
        return 0;
    }
    spx = px - px / 10;
    if (spx < 1) {
        spx = 1;
    }
    if (t->kind == N_FRAC) {
        int bar = px / 16;
        int gap = px / 8;
        int pad = px / 6;
        int inner = 0;
        if (bar < 1) {
            bar = 1;
        }
        if (gap < 1) {
            gap = 1;
        }
        if (pad < 1) {
            pad = 1;
        }
        if (layout(a, t->child, face, spx) != 0 || layout(a, t->sub, face, spx) != 0) {
            return -1;
        }
        inner = larger(a->n[t->child].w, a->n[t->sub].w);
        t->w = grow(inner, pad * 2);
        t->asc = grow(grow(a->n[t->child].asc, a->n[t->child].desc), gap + bar);
        t->desc = grow(gap, grow(a->n[t->sub].asc, a->n[t->sub].desc));
        return 0;
    }
    if (t->kind == N_SQRT) {
        int extra = px / 8;
        int rad = px / 2;
        if (extra < 2) {
            extra = 2;
        }
        if (rad < 4) {
            rad = 4;
        }
        if (layout(a, t->child, face, px) != 0) {
            return -1;
        }
        t->w = grow(rad, grow(a->n[t->child].w, 2));
        t->asc = grow(a->n[t->child].asc, extra);
        t->desc = a->n[t->child].desc;
        if (t->sup >= 0) {
            int deg_px = px * 7 / 10;
            if (deg_px < 1) {
                deg_px = 1;
            }
            if (layout(a, t->sup, face, deg_px) != 0) {
                return -1;
            }
            t->w = grow(t->w, a->n[t->sup].w);
        }
        return 0;
    }
    if (t->kind == N_SCRIPT) {
        int script_px = px * 7 / 10;
        int up = 0;
        int side = 0;
        if (script_px < 1) {
            script_px = 1;
        }
        if (layout(a, t->child, face, px) != 0) {
            return -1;
        }
        if (t->sup >= 0 && layout(a, t->sup, face, script_px) != 0) {
            return -1;
        }
        if (t->sub >= 0 && layout(a, t->sub, face, script_px) != 0) {
            return -1;
        }
        up = a->n[t->child].asc * 2 / 5;
        if (up < 1) {
            up = 1;
        }
        t->w = a->n[t->child].w;
        t->asc = a->n[t->child].asc;
        t->desc = a->n[t->child].desc;
        if (t->sup >= 0) {
            side = larger(side, a->n[t->sup].w);
            t->asc = larger(t->asc, grow(up, a->n[t->sup].asc));
        }
        if (t->sub >= 0) {
            int down = a->n[t->child].desc + a->n[t->sub].asc / 2;
            side = larger(side, a->n[t->sub].w);
            t->desc = larger(t->desc, grow(down, a->n[t->sub].desc));
        }
        t->w = grow(t->w, side);
        return 0;
    }
    return -1;
}

static void paint(const struct Arena *a, int id, int x, int baseline, int px, const struct MathDraw *draw) {
    const struct Node *t = NULL;
    if (id < 0 || draw == NULL || draw->glyph == NULL || draw->rule == NULL) {
        return;
    }
    t = &a->n[id];
    if (t->kind == N_ROW) {
        int c = 0;
        int at = x;
        for (c = t->child; c >= 0; c = a->n[c].next) {
            paint(a, c, at, baseline, px, draw);
            at = grow(at, a->n[c].w);
        }
        return;
    }
    if (t->kind == N_ATOM) {
        if (!space_cp(t->cp)) {
            draw->glyph(draw->user, t->cp, x, baseline, px);
        }
        return;
    }
    if (t->kind == N_FRAC) {
        int spx = px - px / 10;
        int bar = px / 16;
        int gap = px / 8;
        int num_w = 0;
        int den_w = 0;
        int bar_top = 0;
        if (spx < 1) {
            spx = 1;
        }
        if (bar < 1) {
            bar = 1;
        }
        if (gap < 1) {
            gap = 1;
        }
        num_w = a->n[t->child].w;
        den_w = a->n[t->sub].w;
        bar_top = baseline - bar;
        paint(a, t->child, x + (t->w - num_w) / 2, bar_top - gap - a->n[t->child].desc, spx, draw);
        paint(a, t->sub, x + (t->w - den_w) / 2, baseline + gap + a->n[t->sub].asc, spx, draw);
        draw->rule(draw->user, x + (t->w - larger(num_w, den_w)) / 2, bar_top, larger(num_w, den_w), bar);
        return;
    }
    if (t->kind == N_SQRT) {
        int extra = px / 8;
        int rad = px / 2;
        int body_x = x;
        int thick = 0;
        if (extra < 2) {
            extra = 2;
        }
        if (rad < 4) {
            rad = 4;
        }
        thick = extra > 2 ? 2 : 1;
        if (t->sup >= 0) {
            int deg_px = px * 7 / 10;
            if (deg_px < 1) {
                deg_px = 1;
            }
            paint(a, t->sup, x, baseline - a->n[t->child].asc, deg_px, draw);
            body_x = grow(body_x, a->n[t->sup].w);
        }
        body_x = grow(body_x, rad);
        paint(a, t->child, body_x, baseline, px, draw);
        draw->rule(draw->user, x, baseline + (t->desc > 2 ? t->desc / 3 : 0), rad / 3, thick);
        draw->rule(draw->user, x + rad / 3, baseline - t->asc, thick, t->asc + t->desc);
        draw->rule(draw->user, body_x, baseline - t->asc, grow(a->n[t->child].w, 2), thick);
        return;
    }
    if (t->kind == N_SCRIPT) {
        int script_px = px * 7 / 10;
        int up = a->n[t->child].asc * 2 / 5;
        if (script_px < 1) {
            script_px = 1;
        }
        if (up < 1) {
            up = 1;
        }
        paint(a, t->child, x, baseline, px, draw);
        if (t->sup >= 0) {
            paint(a, t->sup, x + a->n[t->child].w, baseline - up, script_px, draw);
        }
        if (t->sub >= 0) {
            int down = a->n[t->child].desc + a->n[t->sub].asc / 2;
            paint(a, t->sub, x + a->n[t->child].w, baseline + down, script_px, draw);
        }
    }
}

static int build(struct Arena *a, const char *s, size_t n, const struct MathFace *face, int px, int *root) {
    size_t i = 0;
    int row = 0;
    if (s == NULL || n == 0 || n > 1024 || face == NULL || px < 1 || root == NULL) {
        return -1;
    }
    memset(a, 0, sizeof *a);
    row = parse_row(a, s, n, &i, 0);
    if (row < 0) {
        return -1;
    }
    while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n')) {
        i++;
    }
    if (i != n) {
        return -1;
    }
    if (layout(a, row, face, px) != 0) {
        return -1;
    }
    *root = row;
    return 0;
}

int math_measure(const char *s, size_t n, const struct MathFace *face, int px, int *width, int *ascent, int *descent) {
    struct Arena a;
    int root = 0;
    if (width == NULL || ascent == NULL || descent == NULL) {
        return -1;
    }
    *width = 0;
    *ascent = 0;
    *descent = 0;
    if (build(&a, s, n, face, px, &root) != 0) {
        return -1;
    }
    *width = a.n[root].w;
    *ascent = a.n[root].asc;
    *descent = a.n[root].desc;
    return 0;
}

int math_draw(const char *s, size_t n, const struct MathFace *face, int px, int origin_x, int baseline,
              const struct MathDraw *draw) {
    struct Arena a;
    int root = 0;
    if (draw == NULL) {
        return -1;
    }
    if (build(&a, s, n, face, px, &root) != 0) {
        return -1;
    }
    paint(&a, root, origin_x, baseline, px, draw);
    return 0;
}

static int oadd(struct Out *o, const char *z) {
    size_t k = strlen(z);
    if (o->s == NULL || k >= o->cap - o->n) {
        return -1;
    }
    memcpy(o->s + o->n, z, k);
    o->n += k;
    o->s[o->n] = '\0';
    return 0;
}

static int ocp(struct Out *o, unsigned cp) {
    char buf[8];
    int len = 0;
    if (cp == '&') {
        return oadd(o, "&amp;");
    }
    if (cp == '<') {
        return oadd(o, "&lt;");
    }
    if (cp == '>') {
        return oadd(o, "&gt;");
    }
    len = utf8_encode(cp, buf);
    if (len < 0 || (size_t)len >= o->cap - o->n) {
        return -1;
    }
    memcpy(o->s + o->n, buf, (size_t)len);
    o->n += (size_t)len;
    o->s[o->n] = '\0';
    return 0;
}

static int emit_ml(const struct Arena *a, int id, struct Out *o) {
    const struct Node *t = NULL;
    int c = 0;
    if (id < 0) {
        return -1;
    }
    t = &a->n[id];
    if (t->kind == N_ROW) {
        int kids = 0;
        for (c = t->child; c >= 0; c = a->n[c].next) {
            kids++;
        }
        if (kids > 1 && oadd(o, "<mrow>") != 0) {
            return -1;
        }
        for (c = t->child; c >= 0; c = a->n[c].next) {
            if (emit_ml(a, c, o) != 0) {
                return -1;
            }
        }
        if (kids > 1 && oadd(o, "</mrow>") != 0) {
            return -1;
        }
        return 0;
    }
    if (t->kind == N_ATOM) {
        const char *tag = "mo";
        if (space_cp(t->cp)) {
            return oadd(o, "<mspace/>");
        }
        if ((t->cp >= '0' && t->cp <= '9')) {
            tag = "mn";
        } else if ((t->cp >= 'A' && t->cp <= 'Z') || (t->cp >= 'a' && t->cp <= 'z')) {
            tag = "mi";
        }
        if (oadd(o, "<") != 0 || oadd(o, tag) != 0 || oadd(o, ">") != 0 || ocp(o, t->cp) != 0 || oadd(o, "</") != 0 ||
            oadd(o, tag) != 0 || oadd(o, ">") != 0) {
            return -1;
        }
        return 0;
    }
    if (t->kind == N_FRAC) {
        if (oadd(o, "<mfrac>") != 0 || emit_ml(a, t->child, o) != 0 || emit_ml(a, t->sub, o) != 0 ||
            oadd(o, "</mfrac>") != 0) {
            return -1;
        }
        return 0;
    }
    if (t->kind == N_SQRT) {
        if (t->sup >= 0) {
            if (oadd(o, "<mroot>") != 0 || emit_ml(a, t->child, o) != 0 || emit_ml(a, t->sup, o) != 0 ||
                oadd(o, "</mroot>") != 0) {
                return -1;
            }
            return 0;
        }
        if (oadd(o, "<msqrt>") != 0 || emit_ml(a, t->child, o) != 0 || oadd(o, "</msqrt>") != 0) {
            return -1;
        }
        return 0;
    }
    if (t->kind == N_SCRIPT) {
        const char *open = "<msup>";
        const char *close = "</msup>";
        if (t->sup >= 0 && t->sub >= 0) {
            open = "<msubsup>";
            close = "</msubsup>";
        } else if (t->sub >= 0) {
            open = "<msub>";
            close = "</msub>";
        }
        if (oadd(o, open) != 0 || emit_ml(a, t->child, o) != 0) {
            return -1;
        }
        if (t->sub >= 0 && t->sup >= 0) {
            if (emit_ml(a, t->sub, o) != 0 || emit_ml(a, t->sup, o) != 0) {
                return -1;
            }
        } else if (t->sup >= 0) {
            if (emit_ml(a, t->sup, o) != 0) {
                return -1;
            }
        } else if (emit_ml(a, t->sub, o) != 0) {
            return -1;
        }
        return oadd(o, close);
    }
    return -1;
}

int math_mathml(const char *s, size_t n, int display, char *out, size_t cap) {
    struct Arena a;
    struct Out o;
    size_t i = 0;
    int root = 0;
    if (out == NULL || cap < 16 || s == NULL || n == 0 || n > 1024) {
        return -1;
    }
    out[0] = '\0';
    memset(&a, 0, sizeof a);
    root = parse_row(&a, s, n, &i, 0);
    if (root < 0) {
        return -1;
    }
    while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n')) {
        i++;
    }
    if (i != n) {
        return -1;
    }
    o.s = out;
    o.n = 0;
    o.cap = cap;
    if (oadd(&o, display ? "<math display=\"block\">" : "<math>") != 0 || emit_ml(&a, root, &o) != 0 ||
        oadd(&o, "</math>") != 0) {
        out[0] = '\0';
        return -1;
    }
    return 0;
}
