#include "page.h"

#include "utf8.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wcast-qual"
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#pragma GCC diagnostic pop

static stbtt_fontinfo g_font;
static int g_font_ok;
static int g_typewriter;
static int g_focus;
static int g_source;
static int g_theme;
static int g_dim;
static int g_seek;
static int g_seek_x;
static int g_seek_row;
static int g_seek_dist;
static size_t g_seek_at;
static uint64_t g_want;
static int g_follow = 1;
static int g_bar_x;
static int g_bar_y;
static int g_bar_h;
static int g_pad;
static int g_line_h = 1;
static int g_drag_px;

static void consider(size_t at, int gx, int row) {
    if (!g_seek || row != g_seek_row) {
        return;
    }
    int dist = gx - g_seek_x;
    if (dist < 0) {
        dist = -dist;
    }
    if (dist < g_seek_dist) {
        g_seek_dist = dist;
        g_seek_at = at;
    }
}

enum {
    SLOT_PAPER,
    SLOT_INK,
    SLOT_HEAD,
    SLOT_EM,
    SLOT_CODE,
    SLOT_LINK,
    SLOT_LIST,
    SLOT_QUOTE,
    SLOT_SEL,
    SLOT_BLOCK,
    SLOT_STRIKE,
    SLOT_RULE,
    SLOT_COUNT
};

static const int g_themes[2][SLOT_COUNT][3] = {
    {{PAGE_PAPER_R, PAGE_PAPER_G, PAGE_PAPER_B},
     {PAGE_INK_R, PAGE_INK_G, PAGE_INK_B},
     {PAGE_HEAD_R, PAGE_HEAD_G, PAGE_HEAD_B},
     {PAGE_EM_R, PAGE_EM_G, PAGE_EM_B},
     {PAGE_CODE_R, PAGE_CODE_G, PAGE_CODE_B},
     {PAGE_LINK_R, PAGE_LINK_G, PAGE_LINK_B},
     {PAGE_LIST_R, PAGE_LIST_G, PAGE_LIST_B},
     {PAGE_QUOTE_R, PAGE_QUOTE_G, PAGE_QUOTE_B},
     {PAGE_SEL_R, PAGE_SEL_G, PAGE_SEL_B},
     {PAGE_BLOCK_R, PAGE_BLOCK_G, PAGE_BLOCK_B},
     {PAGE_STRIKE_R, PAGE_STRIKE_G, PAGE_STRIKE_B},
     {PAGE_RULE_R, PAGE_RULE_G, PAGE_RULE_B}},
    {{PAGE_NIGHT_PAPER_R, PAGE_NIGHT_PAPER_G, PAGE_NIGHT_PAPER_B},
     {PAGE_NIGHT_INK_R, PAGE_NIGHT_INK_G, PAGE_NIGHT_INK_B},
     {232, 170, 110},
     {220, 150, 190},
     {140, 200, 160},
     {140, 170, 230},
     {190, 190, 120},
     {180, 160, 210},
     {70, 90, 120},
     {48, 44, 68},
     {210, 140, 120},
     {150, 180, 200}}};

static void slot_rgb(int slot, int *r, int *g, int *b) {
    int theme = g_theme == 1 ? 1 : 0;
    if (slot < 0 || slot >= SLOT_COUNT) {
        slot = SLOT_INK;
    }
    *r = g_themes[theme][slot][0];
    *g = g_themes[theme][slot][1];
    *b = g_themes[theme][slot][2];
}

static void apply_dim(int *r, int *g, int *b) {
    if (!g_dim) {
        return;
    }
    int pr, pg, pb;
    slot_rgb(SLOT_PAPER, &pr, &pg, &pb);
    *r = (*r + pr) / 2;
    *g = (*g + pg) / 2;
    *b = (*b + pb) / 2;
}

void page_set_theme(int theme) {
    g_theme = theme == 1 ? 1 : 0;
}

void page_set_typewriter(int on) {
    g_typewriter = on ? 1 : 0;
}

void page_set_focus(int on) {
    g_focus = on ? 1 : 0;
}

void page_set_source(int on) {
    g_source = on ? 1 : 0;
}

int page_typewriter(void) {
    return g_typewriter;
}

int page_focus(void) {
    return g_focus;
}

int page_source(void) {
    return g_source;
}

int page_theme(void) {
    return g_theme;
}

int page_set_font(const unsigned char *ttf, int len) {
    g_font_ok = 0;
    if (ttf == NULL || len <= 0) {
        return -1;
    }
    int offset = stbtt_GetFontOffsetForIndex(ttf, 0);
    if (offset < 0 || !stbtt_InitFont(&g_font, ttf, offset)) {
        return -1;
    }
    g_font_ok = 1;
    return 0;
}

int page_margin(int font_px) {
    int margin = font_px / 2;
    if (margin < 8) {
        margin = 8;
    }
    return margin;
}

static void fill_paper(uint8_t *px, int count) {
    int r, g, b;
    slot_rgb(SLOT_PAPER, &r, &g, &b);
    for (int i = 0; i < count; i++) {
        px[i * 4 + 0] = (uint8_t)r;
        px[i * 4 + 1] = (uint8_t)g;
        px[i * 4 + 2] = (uint8_t)b;
        px[i * 4 + 3] = 255;
    }
}

static void blend(uint8_t *px, int width, int height, int x, int y, int coverage, int r, int g, int b) {
    if (x < 0 || y < 0 || x >= width || y >= height || coverage <= 0) {
        return;
    }
    apply_dim(&r, &g, &b);
    uint8_t *p = px + ((size_t)y * (size_t)width + (size_t)x) * 4;
    int inv = 255 - coverage;
    p[0] = (uint8_t)((r * coverage + p[0] * inv) / 255);
    p[1] = (uint8_t)((g * coverage + p[1] * inv) / 255);
    p[2] = (uint8_t)((b * coverage + p[2] * inv) / 255);
    p[3] = 255;
}

static void fill_rect(uint8_t *px, int width, int height, int x, int y, int w, int h, int r, int g, int b) {
    if (w <= 0 || h <= 0) {
        return;
    }
    apply_dim(&r, &g, &b);
    for (int row = 0; row < h; row++) {
        int yy = y + row;
        if (yy < 0 || yy >= height) {
            continue;
        }
        for (int col = 0; col < w; col++) {
            int xx = x + col;
            if (xx < 0 || xx >= width) {
                continue;
            }
            uint8_t *p = px + ((size_t)yy * (size_t)width + (size_t)xx) * 4;
            p[0] = (uint8_t)r;
            p[1] = (uint8_t)g;
            p[2] = (uint8_t)b;
            p[3] = 255;
        }
    }
}

static void solid_bar(uint8_t *px, int width, int height, int x, int y, int w, int h) {
    int r, g, b;
    slot_rgb(SLOT_INK, &r, &g, &b);
    int saved = g_dim;
    g_dim = 0;
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            blend(px, width, height, x + col, y + row, 255, r, g, b);
        }
    }
    g_dim = saved;
}

enum { KIND_BODY = 0, KIND_HEAD = 1, KIND_EM = 2, KIND_CODE = 3, KIND_LINK = 4, KIND_LIST = 5, KIND_QUOTE = 6,
       KIND_STRIKE = 7, KIND_RULE = 8, KIND_TABLE = 9, KIND_IMAGE = 10, KIND_SKIP = 11 };

enum { TABLE_COLS = 8 };

struct Line {
    size_t off;
    size_t content;
    uint32_t rows;
    uint8_t kind;
};

struct Index {
    struct Line *lines;
    size_t n;
    size_t cap;
    int caret_x;
    uint64_t caret_y;
};

static void color_of(int kind, int *r, int *g, int *b) {
    int slot = SLOT_INK;
    switch (kind) {
    case KIND_HEAD:
        slot = SLOT_HEAD;
        break;
    case KIND_EM:
        slot = SLOT_EM;
        break;
    case KIND_CODE:
        slot = SLOT_CODE;
        break;
    case KIND_LINK:
        slot = SLOT_LINK;
        break;
    case KIND_LIST:
        slot = SLOT_LIST;
        break;
    case KIND_QUOTE:
        slot = SLOT_QUOTE;
        break;
    case KIND_STRIKE:
        slot = SLOT_STRIKE;
        break;
    case KIND_RULE:
        slot = SLOT_RULE;
        break;
    case KIND_IMAGE:
        slot = SLOT_LINK;
        break;
    default:
        break;
    }
    slot_rgb(slot, r, g, b);
}

static int add_rows(uint64_t y, uint32_t rows, int line_h, uint64_t *out) {
    if (line_h < 1 || rows == 0) {
        *out = y;
        return 0;
    }
    if ((uint64_t)rows > UINT64_MAX / (uint64_t)line_h) {
        return -1;
    }
    uint64_t span = (uint64_t)rows * (uint64_t)line_h;
    if (y > UINT64_MAX - span) {
        return -1;
    }
    *out = y + span;
    return 0;
}

static int index_push(struct Index *idx, struct Line line) {
    if (idx->n == idx->cap) {
        if (idx->cap > SIZE_MAX / 2 / sizeof(struct Line)) {
            return -1;
        }
        size_t cap = idx->cap == 0 ? 64 : idx->cap * 2;
        struct Line *grown = realloc(idx->lines, cap * sizeof *grown);
        if (grown == NULL) {
            return -1;
        }
        idx->lines = grown;
        idx->cap = cap;
    }
    idx->lines[idx->n++] = line;
    return 0;
}

/* A fence is three backticks or three tildes. The close must use the same mark. */
static int is_fence(const char *s, size_t n) {
    size_t i = 0;
    while (i < n && i < 3 && s[i] == ' ') {
        i++;
    }
    if (i + 2 >= n) {
        return 0;
    }
    char mark = s[i];
    if ((mark == '`' || mark == '~') && s[i + 1] == mark && s[i + 2] == mark) {
        return mark;
    }
    return 0;
}

static int is_heading(const char *s, size_t n) {
    size_t i = 0;
    while (i < n && i < 3 && s[i] == ' ') {
        i++;
    }
    if (i >= n || s[i] != '#') {
        return 0;
    }
    size_t hashes = i;
    while (hashes < n && s[hashes] == '#' && hashes - i < 6) {
        hashes++;
    }
    if (hashes == i) {
        return 0;
    }
    return hashes == n || s[hashes] == ' ' || s[hashes] == '\t';
}

/* Three or more of the same -, *, or _, spaces allowed. */
static int is_rule(const char *s, size_t n) {
    size_t i = 0;
    while (i < n && i < 3 && s[i] == ' ') {
        i++;
    }
    if (i >= n) {
        return 0;
    }
    char mark = s[i];
    if (mark != '-' && mark != '*' && mark != '_') {
        return 0;
    }
    int count = 0;
    for (; i < n; i++) {
        if (s[i] == mark) {
            count++;
        } else if (s[i] != ' ' && s[i] != '\t') {
            return 0;
        }
    }
    return count >= 3;
}

/* A line of = or - under a paragraph. At least one marker. */
static int is_setext(const char *s, size_t n) {
    size_t i = 0;
    while (i < n && i < 3 && s[i] == ' ') {
        i++;
    }
    if (i >= n || (s[i] != '=' && s[i] != '-')) {
        return 0;
    }
    char mark = s[i];
    int count = 0;
    for (; i < n; i++) {
        if (s[i] == mark) {
            count++;
        } else if (s[i] != ' ') {
            return 0;
        }
    }
    return count >= 1;
}

/* shortcut: a table cell cannot contain a raw newline, and \| is not an escaped pipe. At most 8 columns. */
static int line_has_pipe(const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '|') {
            return 1;
        }
    }
    return 0;
}

static int split_row(const char *s, size_t n, size_t *at, size_t *len, int cap) {
    size_t i = 0;
    while (i < n && (s[i] == ' ' || s[i] == '\t')) {
        i++;
    }
    if (i < n && s[i] == '|') {
        i++;
    }
    int count = 0;
    while (count < cap) {
        size_t start = i;
        while (i < n && s[i] != '|') {
            i++;
        }
        size_t a = start;
        size_t z = i;
        while (a < z && (s[a] == ' ' || s[a] == '\t')) {
            a++;
        }
        while (z > a && (s[z - 1] == ' ' || s[z - 1] == '\t')) {
            z--;
        }
        at[count] = a;
        len[count] = z - a;
        count++;
        if (i >= n) {
            break;
        }
        i++;
        if (i >= n) {
            break;
        }
    }
    return count;
}

static int delim_cell(const char *s, size_t n, int *align) {
    size_t i = 0;
    while (i < n && (s[i] == ' ' || s[i] == '\t')) {
        i++;
    }
    size_t end = n;
    while (end > i && (s[end - 1] == ' ' || s[end - 1] == '\t')) {
        end--;
    }
    if (i >= end) {
        return 0;
    }
    int left = 0;
    int right = 0;
    if (s[i] == ':') {
        left = 1;
        i++;
    }
    size_t dashes = 0;
    while (i < end && s[i] == '-') {
        dashes++;
        i++;
    }
    if (i < end && s[i] == ':') {
        right = 1;
        i++;
    }
    if (i != end || dashes < 1) {
        return 0;
    }
    if (align != NULL) {
        *align = left && right ? 1 : (right ? 2 : 0);
    }
    return 1;
}

static int delim_row(const char *s, size_t n, int *align, int *cols) {
    size_t at[TABLE_COLS];
    size_t len[TABLE_COLS];
    int count = split_row(s, n, at, len, TABLE_COLS);
    if (count < 1) {
        return 0;
    }
    for (int c = 0; c < count; c++) {
        int how = 0;
        if (!delim_cell(s + at[c], len[c], &how)) {
            return 0;
        }
        if (align != NULL) {
            align[c] = how;
        }
    }
    if (cols != NULL) {
        *cols = count;
    }
    return 1;
}

static void mark_tables(const char *text, struct Index *idx) {
    size_t i = 0;
    while (i + 1 < idx->n) {
        struct Line *head = &idx->lines[i];
        struct Line *delim = &idx->lines[i + 1];
        if (head->kind == KIND_CODE || delim->kind == KIND_CODE || !line_has_pipe(text + head->off, head->content) ||
            !delim_row(text + delim->off, delim->content, NULL, NULL)) {
            i++;
            continue;
        }
        head->kind = KIND_TABLE;
        delim->kind = KIND_TABLE;
        size_t j = i + 2;
        while (j < idx->n && idx->lines[j].kind != KIND_CODE && line_has_pipe(text + idx->lines[j].off, idx->lines[j].content)) {
            idx->lines[j].kind = KIND_TABLE;
            j++;
        }
        i = j;
    }
}

/* shortcut: emphasis is a same-line * or _ pair, not CommonMark flanking rules.
   Inline marks are skipped on a line longer than 256 KiB. */
static void mark_inline(const char *s, size_t n, uint8_t *mark) {
    memset(mark, KIND_BODY, n);
    size_t i = 0;
    while (i < n) {
        if (s[i] == '`') {
            size_t j = i + 1;
            while (j < n && s[j] != '`') {
                j++;
            }
            if (j < n) {
                for (size_t k = i; k <= j; k++) {
                    mark[k] = KIND_CODE;
                }
                i = j + 1;
                continue;
            }
        }
        if (s[i] == '~' && i + 1 < n && s[i + 1] == '~') {
            size_t j = i + 2;
            while (j + 1 < n && !(s[j] == '~' && s[j + 1] == '~')) {
                j++;
            }
            if (j + 1 < n) {
                for (size_t k = i; k <= j + 1; k++) {
                    mark[k] = KIND_STRIKE;
                }
                i = j + 2;
                continue;
            }
        }
        if (s[i] == '!' && i + 1 < n && s[i + 1] == '[') {
            size_t bracket = i + 2;
            while (bracket < n && s[bracket] != ']' && s[bracket] != '\n') {
                bracket++;
            }
            if (bracket + 1 < n && s[bracket] == ']' && s[bracket + 1] == '(') {
                size_t end = bracket + 2;
                while (end < n && s[end] != ')') {
                    end++;
                }
                if (end < n) {
                    mark[i] = KIND_SKIP;
                    mark[i + 1] = KIND_SKIP;
                    for (size_t k = i + 2; k < bracket; k++) {
                        mark[k] = KIND_IMAGE;
                    }
                    for (size_t k = bracket; k <= end; k++) {
                        mark[k] = KIND_SKIP;
                    }
                    i = end + 1;
                    continue;
                }
            }
        }
        if (s[i] == '[') {
            size_t bracket = i + 1;
            while (bracket < n && s[bracket] != ']' && s[bracket] != '\n') {
                bracket++;
            }
            if (bracket + 1 < n && s[bracket] == ']' && s[bracket + 1] == '(') {
                size_t end = bracket + 2;
                while (end < n && s[end] != ')') {
                    end++;
                }
                if (end < n) {
                    for (size_t k = i; k <= end; k++) {
                        mark[k] = KIND_LINK;
                    }
                    i = end + 1;
                    continue;
                }
            }
        }
        if ((s[i] == '*' || s[i] == '_') && i + 1 < n) {
            int strong = s[i + 1] == s[i];
            char marker = s[i];
            size_t j = i + (strong ? 2 : 1);
            int found = 0;
            while (j < n) {
                if (strong && j + 1 < n && s[j] == marker && s[j + 1] == marker) {
                    found = 1;
                    break;
                }
                if (!strong && s[j] == marker && (j + 1 >= n || s[j + 1] != marker)) {
                    found = 1;
                    break;
                }
                j++;
            }
            if (found) {
                size_t end = strong ? j + 1 : j;
                for (size_t k = i; k <= end && k < n; k++) {
                    mark[k] = KIND_EM;
                }
                i = end + 1;
                continue;
            }
        }
        i++;
    }
}

/* Mark a list or quote marker. The rest of the line keeps its inline marks. */
static void mark_block(const char *s, size_t n, uint8_t *mark) {
    size_t i = 0;
    while (i < n && i < 3 && s[i] == ' ') {
        i++;
    }
    if (i < n && s[i] == '>') {
        while (i < n && s[i] == '>') {
            mark[i] = KIND_QUOTE;
            i++;
        }
        if (i < n && s[i] == ' ') {
            mark[i] = KIND_QUOTE;
            i++;
        }
        while (i < n && i < 6 && s[i] == ' ') {
            i++;
        }
    }
    if (i < n && (s[i] == '-' || s[i] == '+' || s[i] == '*') && i + 1 < n && s[i + 1] == ' ') {
        mark[i] = KIND_LIST;
        mark[i + 1] = KIND_LIST;
        return;
    }
    size_t digits = i;
    while (digits < n && digits - i < 9 && s[digits] >= '0' && s[digits] <= '9') {
        digits++;
    }
    if (digits > i && digits + 1 < n && s[digits] == '.' && s[digits + 1] == ' ') {
        for (size_t k = i; k <= digits + 1; k++) {
            mark[k] = KIND_LIST;
        }
    }
}

static size_t heading_prefix(const char *s, size_t n) {
    size_t i = 0;
    while (i < n && i < 3 && s[i] == ' ') {
        i++;
    }
    if (i >= n || s[i] != '#') {
        return 0;
    }
    size_t hashes = i;
    while (hashes < n && s[hashes] == '#' && hashes - i < 6) {
        hashes++;
    }
    if (hashes == i || (hashes < n && s[hashes] != ' ' && s[hashes] != '\t')) {
        return 0;
    }
    if (hashes < n) {
        hashes++;
    }
    return hashes;
}

/* Bytes of a quote marker and a following list marker. Leading indent is included. */
static size_t block_prefix(const char *s, size_t n, int *quote, int *list, int *indent) {
    *quote = 0;
    *list = 0;
    *indent = 0;
    size_t i = 0;
    while (i < n && i < 24 && s[i] == ' ') {
        i++;
    }
    *indent = (int)i;
    int any = 0;
    if (i < n && s[i] == '>') {
        *quote = 1;
        any = 1;
        while (i < n && s[i] == '>') {
            i++;
        }
        if (i < n && s[i] == ' ') {
            i++;
        }
        while (i < n && i < 6 && s[i] == ' ') {
            i++;
        }
    }
    if (i < n && (s[i] == '-' || s[i] == '+' || s[i] == '*') && i + 1 < n && s[i + 1] == ' ') {
        *list = 1;
        any = 1;
        i += 2;
        if (i + 3 < n && s[i] == '[' && (s[i + 1] == ' ' || s[i + 1] == 'x' || s[i + 1] == 'X') && s[i + 2] == ']' &&
            s[i + 3] == ' ') {
            *list = s[i + 1] == ' ' ? 2 : 3;
            i += 4;
        }
    } else {
        size_t digits = i;
        while (digits < n && digits - i < 9 && s[digits] >= '0' && s[digits] <= '9') {
            digits++;
        }
        if (digits > i && digits + 1 < n && s[digits] == '.' && s[digits + 1] == ' ') {
            *list = 1;
            any = 1;
            i = digits + 2;
        }
    }
    return any ? i : 0;
}

static int row_visible(uint64_t row_y, uint64_t scroll, int line_h, int view_h) {
    uint64_t bottom = 0;
    if (add_rows(row_y, 1, line_h, &bottom) != 0) {
        return 0;
    }
    uint64_t limit = scroll;
    if ((uint64_t)view_h > UINT64_MAX - limit) {
        limit = UINT64_MAX;
    } else {
        limit += (uint64_t)view_h;
    }
    if ((uint64_t)line_h <= UINT64_MAX - limit) {
        limit += (uint64_t)line_h;
    }
    return bottom > scroll && row_y < limit;
}

static int screen_row(uint64_t row_y, uint64_t scroll, int origin_y) {
    if (row_y >= scroll) {
        uint64_t rel = row_y - scroll;
        if (rel > (uint64_t)INT_MAX || (uint64_t)origin_y > (uint64_t)INT_MAX - rel) {
            return INT_MIN;
        }
        return origin_y + (int)rel + g_pad;
    }
    uint64_t above = scroll - row_y;
    if (above > (uint64_t)INT_MAX) {
        return INT_MIN;
    }
    return origin_y - (int)above + g_pad;
}

/* Lay out one newline-row. Heights accumulate in the caller as uint64 so a tall note
   cannot wrap a signed pixel coordinate. Returns the visual row count, at least 1. */
static uint32_t layout_line(uint8_t *px, int width, int height, const char *text, size_t off, size_t len,
                            size_t caret, int origin_x, int max_x, int origin_y, int line_h, int baseline,
                            int space_px, float scale, int draw, uint64_t row0, uint64_t scroll, int view_h,
                            size_t sel_lo, size_t sel_hi, int source, const uint8_t *mark, int uniform,
                            int *caret_x, int *caret_row) {
    int x = origin_x;
    int prev = 0;
    uint32_t row = 0;
    size_t i = off;
    size_t end = off + len;
    size_t hide = 0;
    if (!source && len > 0) {
        if (uniform == KIND_CODE && is_fence(text + off, len)) {
            hide = len;
        } else if (uniform == KIND_HEAD) {
            hide = heading_prefix(text + off, len);
            if (hide == 0 && is_setext(text + off, len)) {
                hide = len;
            }
        } else if (uniform == KIND_RULE) {
            hide = len;
            uint64_t row_y = 0;
            int on = add_rows(row0, 0, line_h, &row_y) == 0 && row_visible(row_y, scroll, line_h, view_h);
            int sy = on ? screen_row(row_y, scroll, origin_y) : INT_MIN;
            int thick = line_h / 12;
            if (thick < 2) {
                thick = 2;
            }
            int span = max_x - origin_x;
            if (span < 1) {
                span = 1;
            }
            if (draw && px != NULL && sy != INT_MIN) {
                int rr, rg, rb;
                slot_rgb(SLOT_RULE, &rr, &rg, &rb);
                fill_rect(px, width, height, origin_x, sy + (line_h - thick) / 2, span, thick, rr, rg, rb);
            }
        } else {
            int quote = 0;
            int list = 0;
            int indent = 0;
            hide = block_prefix(text + off, len, &quote, &list, &indent);
            if (indent > 0) {
                int shift = indent * space_px / 2;
                if (x <= INT_MAX - shift) {
                    x += shift;
                }
            }
            if (quote || list) {
                uint64_t row_y = 0;
                int on = add_rows(row0, 0, line_h, &row_y) == 0 && row_visible(row_y, scroll, line_h, view_h);
                int sy = on ? screen_row(row_y, scroll, origin_y) : INT_MIN;
                if (quote) {
                    int bar = line_h / 10;
                    if (bar < 3) {
                        bar = 3;
                    }
                    if (draw && px != NULL && sy != INT_MIN) {
                        int qr, qg, qb;
                        slot_rgb(SLOT_QUOTE, &qr, &qg, &qb);
                        fill_rect(px, width, height, origin_x, sy, bar, line_h, qr, qg, qb);
                    }
                    x = origin_x + bar + space_px;
                }
                if (list) {
                    int side = line_h / 5;
                    if (side < 4) {
                        side = 4;
                    }
                    if (draw && px != NULL && sy != INT_MIN) {
                        int by = sy + (line_h - side) / 2;
                        int lr, lg, lb;
                        slot_rgb(SLOT_LIST, &lr, &lg, &lb);
                        if (list == 1) {
                            fill_rect(px, width, height, x, by, side, side, lr, lg, lb);
                        } else {
                            int t = side / 6;
                            if (t < 2) {
                                t = 2;
                            }
                            fill_rect(px, width, height, x, by, side, t, lr, lg, lb);
                            fill_rect(px, width, height, x, by + side - t, side, t, lr, lg, lb);
                            fill_rect(px, width, height, x, by, t, side, lr, lg, lb);
                            fill_rect(px, width, height, x + side - t, by, t, side, lr, lg, lb);
                            if (list == 3 && side > t * 3) {
                                fill_rect(px, width, height, x + t, by + t, side - t * 2, side - t * 2, lr, lg, lb);
                            }
                        }
                    }
                    x += side + space_px;
                }
            }
        }
    }
    if (len == 0) {
        if (caret == off) {
            *caret_x = origin_x;
            *caret_row = 0;
        }
        consider(off, origin_x, 0);
        return 1;
    }
    while (i < end) {
        if (i == caret) {
            *caret_x = x;
            *caret_row = row > (uint32_t)INT_MAX ? INT_MAX : (int)row;
        }
        unsigned cp = 0;
        int n = utf8_decode(text + i, end - i, &cp);
        size_t at = i;
        if (n < 0) {
            i++;
            continue;
        }
        i += (size_t)n;
        if (!source && hide > 0 && at < off + hide) {
            consider(at, x, (int)row);
            continue;
        }
        if (!source && mark != NULL && at >= off && (size_t)(at - off) < len &&
            ((mark[at - off] == KIND_STRIKE && text[at] == '~') || mark[at - off] == KIND_SKIP)) {
            consider(at, x, (int)row);
            continue;
        }
        long long advance = space_px;
        int glyph = cp != '\t';
        if (cp == '\t') {
            advance = (long long)space_px * 4;
            glyph = 0;
        }
        int ax = 0;
        int lsb = 0;
        if (glyph) {
            stbtt_GetCodepointHMetrics(&g_font, (int)cp, &ax, &lsb);
            advance = (long long)(ax * scale + 0.5f);
            if (advance < 1) {
                advance = 1;
            }
        }
        long long kern = 0;
        if (glyph && prev != 0 && x > origin_x) {
            kern = (long long)(stbtt_GetCodepointKernAdvance(&g_font, prev, (int)cp) * scale);
        }
        if (x > origin_x && (long long)x + kern + advance > max_x) {
            x = origin_x;
            if (row < UINT32_MAX) {
                row++;
            }
            kern = 0;
        }
        long long draw_x = (long long)x + kern;
        if (draw_x > INT_MAX) {
            draw_x = INT_MAX;
        } else if (draw_x < 0) {
            draw_x = 0;
        }
        x = (int)draw_x;
        if (g_seek) {
            long long right = (long long)x + advance;
            if (right > INT_MAX) {
                right = INT_MAX;
            } else if (right < 0) {
                right = 0;
            }
            consider(at, x, (int)row);
            consider(i, (int)right, (int)row);
        }
        uint64_t row_y = 0;
        int on_screen = add_rows(row0, row, line_h, &row_y) == 0 && row_visible(row_y, scroll, line_h, view_h);
        /* shortcut: the newline that ends a line is not a highlighted cell. */
        int selected = sel_hi > sel_lo && at < sel_hi && at + (size_t)n > sel_lo;
        int kind = mark != NULL && at >= off && at - off < len ? mark[at - off] : uniform;
        if (draw && on_screen && selected && px != NULL && advance > 0 && advance <= width) {
            int sy = screen_row(row_y, scroll, origin_y);
            if (sy != INT_MIN) {
                int sr, sg, sb;
                slot_rgb(SLOT_SEL, &sr, &sg, &sb);
                fill_rect(px, width, height, x, sy, (int)advance, line_h, sr, sg, sb);
            }
        }
        if (draw && on_screen && glyph && px != NULL) {
            int x0, y0, x1, y1;
            stbtt_GetCodepointBitmapBox(&g_font, (int)cp, scale, scale, &x0, &y0, &x1, &y1);
            int gw = x1 - x0;
            int gh = y1 - y0;
            if (gw > 0 && gh > 0 && gw <= 256 && gh <= 256) {
                unsigned char bitmap[256 * 256];
                stbtt_MakeCodepointBitmap(&g_font, bitmap, gw, gh, gw, scale, scale, (int)cp);
                int sy = screen_row(row_y, scroll, origin_y);
                int cr, cg, cb;
                color_of(kind, &cr, &cg, &cb);
                int dest_x = x + x0;
                int dest_y = sy + baseline + y0;
                for (int by = 0; by < gh; by++) {
                    for (int bx = 0; bx < gw; bx++) {
                        blend(px, width, height, dest_x + bx, dest_y + by, bitmap[by * gw + bx], cr, cg, cb);
                    }
                }
            }
        }
        if (draw && on_screen && px != NULL && kind == KIND_STRIKE && text[at] != '~' && advance > 0) {
            int sy = screen_row(row_y, scroll, origin_y);
            if (sy != INT_MIN) {
                int sr, sg, sb;
                slot_rgb(SLOT_STRIKE, &sr, &sg, &sb);
                int thick = line_h / 14;
                if (thick < 2) {
                    thick = 2;
                }
                fill_rect(px, width, height, x, sy + line_h / 2, (int)advance, thick, sr, sg, sb);
            }
        }
        long long next = (long long)x + advance;
        if (next > INT_MAX) {
            next = INT_MAX;
        } else if (next < 0) {
            next = 0;
        }
        x = (int)next;
        if (glyph) {
            prev = (int)cp;
        }
        if (draw && add_rows(row0, row, line_h, &row_y) == 0) {
            uint64_t limit = scroll;
            if ((uint64_t)view_h <= UINT64_MAX - limit) {
                limit += (uint64_t)view_h;
            }
            if ((uint64_t)line_h <= UINT64_MAX - limit && row_y > limit + (uint64_t)line_h) {
                break;
            }
        }
    }
    if (caret == off + len) {
        *caret_x = x;
        *caret_row = row > (uint32_t)INT_MAX ? INT_MAX : (int)row;
    }
    consider(off + len, x, (int)row);
    return row == UINT32_MAX ? UINT32_MAX : row + 1;
}

static int line_kind(const char *text, size_t off, size_t content, int *fence) {
    const char *s = text + off;
    int marker = is_fence(s, content);
    if (marker != 0) {
        if (*fence == 0) {
            *fence = marker;
        } else if (*fence == marker) {
            *fence = 0;
        }
        return KIND_CODE;
    }
    if (*fence) {
        return KIND_CODE;
    }
    if (is_heading(s, content)) {
        return KIND_HEAD;
    }
    if (is_rule(s, content)) {
        return KIND_RULE;
    }
    return KIND_BODY;
}

/* shortcut: the line index is rebuilt on every draw. Patch it on edit when a profile says so. */
static int build_index(struct Index *idx, const char *text, size_t text_len, size_t caret, int origin_x, int max_x,
                       int line_h, int space_px, float scale) {
    idx->caret_x = origin_x;
    idx->caret_y = 0;
    int fence = 0;
    int next_underline = 0;
    size_t i = 0;
    uint64_t y = 0;
    while (i <= text_len) {
        size_t off = i;
        while (i < text_len && text[i] != '\n') {
            i++;
        }
        size_t content = i - off;
        int kind = 0;
        if (next_underline) {
            kind = KIND_HEAD;
            next_underline = 0;
        } else {
            kind = line_kind(text, off, content, &fence);
            if (!fence && kind == KIND_BODY && content > 0 && i < text_len && text[i] == '\n') {
                size_t a = i + 1;
                size_t b = a;
                while (b < text_len && text[b] != '\n') {
                    b++;
                }
                if (is_setext(text + a, b - a)) {
                    kind = KIND_HEAD;
                    next_underline = 1;
                }
            }
        }
        int cx = origin_x;
        int crow = 0;
        uint32_t rows = layout_line(NULL, 0, 0, text, off, content, caret, origin_x, max_x, 0, line_h, 0, space_px,
                                    scale, 0, y, 0, 1, 0, 0, 1, NULL, KIND_BODY, &cx, &crow);
        if (rows == 0) {
            rows = 1;
        }
        struct Line line = {off, content, rows, (uint8_t)kind};
        if (index_push(idx, line) != 0) {
            return -1;
        }
        if (caret >= off && caret <= off + content) {
            uint64_t caret_y = 0;
            uint32_t crow_u = crow < 0 ? 0 : (uint32_t)crow;
            if (add_rows(y, crow_u, line_h, &caret_y) != 0) {
                caret_y = UINT64_MAX;
            }
            idx->caret_x = cx;
            idx->caret_y = caret_y;
        }
        uint64_t next = 0;
        if (add_rows(y, rows, line_h, &next) != 0) {
            return 0;
        }
        y = next;
        if (i >= text_len) {
            break;
        }
        i++;
        if (i == text_len && text[text_len - 1] == '\n') {
            off = text_len;
            content = 0;
            rows = 1;
            struct Line blank = {off, content, rows, (uint8_t)(fence ? KIND_CODE : KIND_BODY)};
            if (index_push(idx, blank) != 0) {
                return -1;
            }
            if (caret >= off) {
                idx->caret_x = origin_x;
                idx->caret_y = y;
            }
            break;
        }
    }
    if (idx->n == 0) {
        struct Line blank = {0, 0, 1, KIND_BODY};
        if (index_push(idx, blank) != 0) {
            return -1;
        }
    }
    mark_tables(text, idx);
    return 0;
}

static uint64_t scroll_of(const struct Index *idx, int line_h, int view_h) {
    uint64_t view = view_h > 0 ? (uint64_t)view_h : 1;
    uint64_t caret_bottom = 0;
    if (add_rows(idx->caret_y, 1, line_h, &caret_bottom) != 0) {
        caret_bottom = UINT64_MAX;
    }
    uint64_t total = 0;
    for (size_t i = 0; i < idx->n; i++) {
        uint64_t next = 0;
        if (add_rows(total, idx->lines[i].rows, line_h, &next) != 0) {
            total = UINT64_MAX;
            break;
        }
        total = next;
    }
    uint64_t max_scroll = total > view ? total - view : 0;
    uint64_t pin = 0;
    g_pad = 0;
    g_line_h = line_h > 0 ? line_h : 1;
    if (g_typewriter) {
        uint64_t half_line = (uint64_t)line_h / 2;
        uint64_t half_view = view / 2;
        uint64_t center = idx->caret_y <= UINT64_MAX - half_line ? idx->caret_y + half_line : UINT64_MAX;
        if (center > half_view) {
            pin = center - half_view;
        } else {
            uint64_t pad = half_view - center;
            g_pad = pad > (uint64_t)INT_MAX ? INT_MAX : (int)pad;
        }
    } else if (caret_bottom > view) {
        pin = caret_bottom - view;
    }
    if (!g_typewriter && pin > idx->caret_y) {
        pin = idx->caret_y;
    }
    uint64_t scroll = pin;
    if (!g_typewriter && !g_follow) {
        scroll = g_want;
        if (scroll > max_scroll) {
            scroll = max_scroll;
        }
    }
    g_want = scroll;
    return scroll;
}

void page_set_follow(int on) {
    g_follow = on ? 1 : 0;
}

void page_scroll_by(int dy) {
    g_follow = 0;
    if (dy > 0) {
        uint64_t add = (uint64_t)dy;
        g_want = g_want > UINT64_MAX - add ? UINT64_MAX : g_want + add;
        return;
    }
    if (dy < 0) {
        uint64_t sub = dy == INT_MIN ? (uint64_t)INT_MAX + 1u : (uint64_t)(-dy);
        g_want = g_want > sub ? g_want - sub : 0;
    }
}

int page_center_drag(int dy) {
    if (!g_typewriter) {
        page_scroll_by(dy);
        return 0;
    }
    if (g_line_h < 1) {
        g_line_h = 1;
    }
    long long sum = (long long)g_drag_px + dy;
    if (sum > INT_MAX) {
        sum = INT_MAX;
    } else if (sum < INT_MIN) {
        sum = INT_MIN;
    }
    g_drag_px = (int)sum;
    int rows = 0;
    while (g_drag_px >= g_line_h && rows < 100000) {
        g_drag_px -= g_line_h;
        rows++;
    }
    while (g_drag_px <= -g_line_h && rows > -100000) {
        g_drag_px += g_line_h;
        rows--;
    }
    return rows;
}

int page_edge(int y, int top, int bottom, int zone) {
    if (zone < 1 || bottom <= top) {
        return 0;
    }
    if (y < top + zone) {
        return -1;
    }
    if (y >= bottom - zone) {
        return 1;
    }
    return 0;
}

int page_caret_near(int x, int y, int slop) {
    if (g_bar_h <= 0) {
        return 0;
    }
    if (slop < 0) {
        slop = 0;
    }
    int dx = x - g_bar_x;
    int dy = y - (g_bar_y + g_bar_h / 2);
    if (dx < 0) {
        dx = -dx;
    }
    if (dy < 0) {
        dy = -dy;
    }
    return dx <= slop && dy <= slop + g_bar_h / 2;
}

static int text_width(const char *s, size_t n, float scale) {
    int width = 0;
    size_t i = 0;
    while (i < n) {
        unsigned cp = 0;
        int k = utf8_decode(s + i, n - i, &cp);
        if (k < 0) {
            break;
        }
        i += (size_t)k;
        int ax = 0;
        int lsb = 0;
        stbtt_GetCodepointHMetrics(&g_font, (int)cp, &ax, &lsb);
        int adv = (int)((float)ax * scale + 0.5f);
        if (adv < 0) {
            adv = 0;
        }
        if (width > 4000 - adv) {
            return 4000;
        }
        width += adv;
    }
    return width;
}

static void table_metrics(const char *text, const struct Index *idx, size_t from, size_t to, float scale, int *width,
                          int *align, int *cols) {
    int n = 0;
    for (int c = 0; c < TABLE_COLS; c++) {
        width[c] = 0;
        align[c] = 0;
    }
    for (size_t li = from; li <= to; li++) {
        const struct Line *line = &idx->lines[li];
        int how[TABLE_COLS];
        int count = 0;
        if (delim_row(text + line->off, line->content, how, &count)) {
            if (count > n) {
                n = count;
            }
            for (int c = 0; c < count; c++) {
                align[c] = how[c];
            }
            continue;
        }
        size_t at[TABLE_COLS];
        size_t len[TABLE_COLS];
        count = split_row(text + line->off, line->content, at, len, TABLE_COLS);
        if (count > n) {
            n = count;
        }
        for (int c = 0; c < count; c++) {
            int w = text_width(text + line->off + at[c], len[c], scale) + 16;
            if (w < 28) {
                w = 28;
            }
            if (w > width[c]) {
                width[c] = w;
            }
        }
    }
    if (n < 1) {
        n = 1;
        width[0] = 28;
    }
    *cols = n;
}

static void paint_table_line(uint8_t *px, int width, int height, const char *text, const struct Index *idx, size_t li,
                             int origin_x, int max_x, int origin_y, int line_h, int baseline, float scale, int space_px,
                             uint64_t row_y, uint64_t scroll, int view_h) {
    size_t from = li;
    size_t to = li;
    while (from > 0 && idx->lines[from - 1].kind == KIND_TABLE) {
        from--;
    }
    while (to + 1 < idx->n && idx->lines[to + 1].kind == KIND_TABLE) {
        to++;
    }
    int col_w[TABLE_COLS];
    int align[TABLE_COLS];
    int cols = 0;
    table_metrics(text, idx, from, to, scale, col_w, align, &cols);
    int need = cols;
    for (int c = 0; c < cols; c++) {
        if (need > 20000 - col_w[c]) {
            need = 20000;
            break;
        }
        need += col_w[c];
    }
    int room = max_x - origin_x;
    if (room < cols) {
        room = cols;
    }
    if (need > room) {
        for (int c = 0; c < cols; c++) {
            col_w[c] = col_w[c] * room / need;
            if (col_w[c] < 8) {
                col_w[c] = 8;
            }
        }
    }
    int table_w = cols;
    for (int c = 0; c < cols; c++) {
        table_w += col_w[c];
    }
    if (!row_visible(row_y, scroll, line_h, view_h)) {
        return;
    }
    int sy = screen_row(row_y, scroll, origin_y);
    if (sy == INT_MIN) {
        return;
    }
    int rr, rg, rb;
    slot_rgb(SLOT_RULE, &rr, &rg, &rb);
    const struct Line *line = &idx->lines[li];
    int header = li == from;
    int delim = delim_row(text + line->off, line->content, NULL, NULL);
    fill_rect(px, width, height, origin_x, sy + line_h - 2, table_w, 2, rr, rg, rb);
    int x = origin_x;
    for (int c = 0; c <= cols; c++) {
        fill_rect(px, width, height, x, sy, 1, line_h, rr, rg, rb);
        if (c < cols) {
            x += col_w[c];
        }
    }
    if (delim) {
        return;
    }
    size_t at[TABLE_COLS];
    size_t len[TABLE_COLS];
    int count = split_row(text + line->off, line->content, at, len, TABLE_COLS);
    x = origin_x + 1;
    for (int c = 0; c < cols; c++) {
        if (c < count && len[c] > 0) {
            int tw = text_width(text + line->off + at[c], len[c], scale);
            int cell = col_w[c] - 2;
            if (cell < 1) {
                cell = 1;
            }
            int ox = x + 8;
            if (c < TABLE_COLS && align[c] == 2) {
                ox = x + cell - tw - 8;
            } else if (c < TABLE_COLS && align[c] == 1) {
                ox = x + (cell - tw) / 2;
            }
            if (ox < x) {
                ox = x;
            }
            int cx = 0;
            int crow = 0;
            layout_line(px, width, height, text, line->off + at[c], len[c], SIZE_MAX, ox, x + cell, origin_y, line_h, baseline,
                        space_px, scale, 1, row_y, scroll, view_h, 0, 0, 1, NULL, header ? KIND_HEAD : KIND_BODY, &cx,
                        &crow);
        }
        x += col_w[c];
    }
}

static int contiguous_kind(const struct Index *idx, size_t a, size_t b, int kind) {
    if (idx->lines[a].kind != kind || idx->lines[b].kind != kind) {
        return 0;
    }
    size_t from = a < b ? a : b;
    size_t to = a < b ? b : a;
    for (size_t k = from; k <= to; k++) {
        if (idx->lines[k].kind != kind) {
            return 0;
        }
    }
    return 1;
}

void page_draw(uint8_t *px, int width, int height, int box_x, int box_y, int box_w, int box_h,
               const char *text, size_t text_len, size_t caret, size_t anchor, int font_px) {
    if (width <= 0 || height <= 0) {
        return;
    }
    fill_paper(px, width * height);
    if (box_w <= 0 || box_h <= 0) {
        box_x = 0;
        box_y = 0;
        box_w = width;
        box_h = height;
    }
    if (!g_font_ok) {
        return;
    }
    if (text == NULL) {
        text = "";
        text_len = 0;
    }
    if (caret > text_len) {
        caret = text_len;
    }
    if (anchor > text_len) {
        anchor = text_len;
    }
    size_t sel_lo = anchor < caret ? anchor : caret;
    size_t sel_hi = anchor < caret ? caret : anchor;
    if (font_px < 8) {
        font_px = 8;
    }
    int margin = page_margin(font_px);
    /* A font taller than the box would otherwise push the caret past the buffer. */
    if (box_h > 4 && margin > box_h / 4) {
        margin = box_h / 4;
    }
    if (box_w > 4 && margin > box_w / 4) {
        margin = box_w / 4;
    }
    int origin_x = box_x + margin;
    int origin_y = box_y + margin;
    int max_x = box_x + box_w - margin;
    int view_h = box_h - margin * 2;
    if (max_x <= origin_x) {
        max_x = origin_x + 1;
    }
    if (view_h < 1) {
        view_h = 1;
    }

    float scale = stbtt_ScaleForPixelHeight(&g_font, (float)font_px);
    int ascent = 0;
    int descent = 0;
    int line_gap = 0;
    stbtt_GetFontVMetrics(&g_font, &ascent, &descent, &line_gap);
    float line_raw = (ascent - descent + line_gap) * scale + 0.999f;
    float base_raw = ascent * scale + 0.5f;
    if (!(line_raw >= 1.f)) {
        line_raw = 1.f;
    }
    if (line_raw > 1000000.f) {
        line_raw = 1000000.f;
    }
    if (base_raw > 1000000.f) {
        base_raw = 1000000.f;
    }
    if (base_raw < -1000000.f) {
        base_raw = -1000000.f;
    }
    int line_h = (int)line_raw;
    int baseline = (int)base_raw;
    int sax = 0;
    int slsb = 0;
    stbtt_GetCodepointHMetrics(&g_font, ' ', &sax, &slsb);
    int space_px = (int)(sax * scale + 0.5f);
    if (space_px < 1) {
        space_px = 1;
    }

    struct Index idx;
    memset(&idx, 0, sizeof idx);
    if (build_index(&idx, text, text_len, caret, origin_x, max_x, line_h, space_px, scale) != 0) {
        free(idx.lines);
        return;
    }
    uint64_t scroll = scroll_of(&idx, line_h, view_h);

    size_t caret_li = 0;
    for (size_t i = 0; i < idx.n; i++) {
        size_t end = idx.lines[i].off + idx.lines[i].content;
        if (caret >= idx.lines[i].off && caret <= end) {
            caret_li = i;
        }
    }
    uint64_t y = 0;
    for (size_t li = 0; li < idx.n; li++) {
        struct Line line = idx.lines[li];
        uint64_t next = 0;
        if (add_rows(y, line.rows, line_h, &next) != 0) {
            break;
        }
        uint64_t window = scroll;
        if ((uint64_t)view_h <= UINT64_MAX - window) {
            window += (uint64_t)view_h;
        }
        if ((uint64_t)line_h <= UINT64_MAX - window) {
            window += (uint64_t)line_h;
        }
        int source = g_source || li == caret_li;
        if (!g_source && !source &&
            (contiguous_kind(&idx, li, caret_li, KIND_CODE) || contiguous_kind(&idx, li, caret_li, KIND_TABLE))) {
            source = 1;
        }
        g_dim = g_focus && !source;
        if (line.kind == KIND_CODE && next > scroll && y < window) {
            for (uint32_t row = 0; row < line.rows; row++) {
                uint64_t row_y = 0;
                if (add_rows(y, row, line_h, &row_y) != 0 || !row_visible(row_y, scroll, line_h, view_h)) {
                    continue;
                }
                int sy = screen_row(row_y, scroll, origin_y);
                int span = max_x - origin_x;
                if (sy != INT_MIN && span > 0) {
                    int br, bg, bb;
                    slot_rgb(SLOT_BLOCK, &br, &bg, &bb);
                    fill_rect(px, width, height, origin_x, sy, span, line_h, br, bg, bb);
                }
            }
        }
        if (!source && line.kind == KIND_TABLE && next > scroll && y < window) {
            paint_table_line(px, width, height, text, &idx, li, origin_x, max_x, origin_y, line_h, baseline, scale,
                             space_px, y, scroll, view_h);
        } else if (next > scroll && y < window && line.content > 0) {
            uint8_t *mark = NULL;
            const uint8_t *marks = NULL;
            if (line.kind == KIND_BODY && line.content <= (1u << 18)) {
                mark = malloc(line.content);
                if (mark != NULL) {
                    mark_inline(text + line.off, line.content, mark);
                    mark_block(text + line.off, line.content, mark);
                    marks = mark;
                }
            }
            int cx = 0;
            int crow = 0;
            /* shortcut: a rendered line keeps the source row count. */
            layout_line(px, width, height, text, line.off, line.content, caret, origin_x, max_x, origin_y, line_h,
                        baseline, space_px, scale, 1, y, scroll, view_h, sel_lo, sel_hi, source, marks, line.kind,
                        &cx, &crow);
            free(mark);
        }
        y = next;
        if (scroll <= UINT64_MAX - (uint64_t)view_h && y > scroll + (uint64_t)view_h + (uint64_t)line_h) {
            break;
        }
    }
    g_dim = 0;
    free(idx.lines);

    uint64_t rel = idx.caret_y - scroll;
    int caret_sy = origin_y;
    if (rel <= (uint64_t)INT_MAX && (uint64_t)origin_y <= (uint64_t)INT_MAX - rel) {
        caret_sy = origin_y + (int)rel;
    }
    if (g_pad > 0 && caret_sy <= INT_MAX - g_pad) {
        caret_sy += g_pad;
    }
    g_bar_x = idx.caret_x;
    g_bar_y = caret_sy;
    g_bar_h = line_h;
    solid_bar(px, width, height, idx.caret_x, caret_sy, 2, line_h);
}

size_t page_hit(int box_x, int box_y, int box_w, int box_h, const char *text, size_t text_len, size_t caret,
                int font_px, int x, int y) {
    if (!g_font_ok || box_w <= 0 || box_h <= 0) {
        return 0;
    }
    if (text == NULL) {
        text = "";
        text_len = 0;
    }
    if (caret > text_len) {
        caret = text_len;
    }
    if (font_px < 8) {
        font_px = 8;
    }
    int margin = page_margin(font_px);
    if (box_h > 4 && margin > box_h / 4) {
        margin = box_h / 4;
    }
    if (box_w > 4 && margin > box_w / 4) {
        margin = box_w / 4;
    }
    int origin_x = box_x + margin;
    int origin_y = box_y + margin;
    int max_x = box_x + box_w - margin;
    int view_h = box_h - margin * 2;
    if (max_x <= origin_x) {
        max_x = origin_x + 1;
    }
    if (view_h < 1) {
        view_h = 1;
    }
    float scale = stbtt_ScaleForPixelHeight(&g_font, (float)font_px);
    int ascent = 0;
    int descent = 0;
    int line_gap = 0;
    stbtt_GetFontVMetrics(&g_font, &ascent, &descent, &line_gap);
    float line_raw = (ascent - descent + line_gap) * scale + 0.999f;
    if (!(line_raw >= 1.f)) {
        line_raw = 1.f;
    }
    if (line_raw > 1000000.f) {
        line_raw = 1000000.f;
    }
    int line_h = (int)line_raw;
    int sax = 0;
    int slsb = 0;
    stbtt_GetCodepointHMetrics(&g_font, ' ', &sax, &slsb);
    int space_px = (int)(sax * scale + 0.5f);
    if (space_px < 1) {
        space_px = 1;
    }

    g_seek = 0;
    struct Index idx;
    memset(&idx, 0, sizeof idx);
    if (build_index(&idx, text, text_len, caret, origin_x, max_x, line_h, space_px, scale) != 0 || idx.n == 0) {
        free(idx.lines);
        return 0;
    }
    uint64_t scroll = scroll_of(&idx, line_h, view_h);
    if (y < origin_y + g_pad) {
        free(idx.lines);
        return 0;
    }
    uint64_t doc_y = (uint64_t)(y - origin_y - g_pad) + scroll;
    uint64_t line_y = 0;
    size_t li = idx.n - 1;
    int inside = 0;
    for (size_t i = 0; i < idx.n; i++) {
        uint64_t next = 0;
        if (add_rows(line_y, idx.lines[i].rows, line_h, &next) != 0) {
            break;
        }
        if (doc_y < next) {
            li = i;
            inside = 1;
            break;
        }
        line_y = next;
    }
    if (!inside) {
        free(idx.lines);
        return text_len;
    }
    size_t caret_li = 0;
    for (size_t i = 0; i < idx.n; i++) {
        size_t end = idx.lines[i].off + idx.lines[i].content;
        if (caret >= idx.lines[i].off && caret <= end) {
            caret_li = i;
        }
    }
    struct Line line = idx.lines[li];
    int source = g_source || li == caret_li;
    if (!g_source && !source &&
        (contiguous_kind(&idx, li, caret_li, KIND_CODE) || contiguous_kind(&idx, li, caret_li, KIND_TABLE))) {
        source = 1;
    }
    uint64_t into = doc_y >= line_y ? doc_y - line_y : 0;
    int hit_row = 0;
    if (line_h > 0 && into / (uint64_t)line_h <= (uint64_t)INT_MAX) {
        hit_row = (int)(into / (uint64_t)line_h);
    }
    g_seek = 1;
    g_seek_x = x;
    g_seek_row = hit_row;
    g_seek_at = line.off;
    g_seek_dist = INT_MAX;
    int cx = 0;
    int crow = 0;
    layout_line(NULL, 0, 0, text, line.off, line.content, caret, origin_x, max_x, origin_y, line_h, 0, space_px,
                scale, 0, line_y, scroll, view_h, 0, 0, source, NULL, line.kind, &cx, &crow);
    g_seek = 0;
    size_t at = g_seek_at;
    free(idx.lines);
    if (at > text_len) {
        at = text_len;
    }
    return at;
}

void page_draw_labels(uint8_t *px, int width, int height, int n, const int *xs, const int *ys, const int *ws,
                      const int *hs, const char *const *labels, int vertical, int font_px) {
    if (px == NULL || !g_font_ok || n <= 0 || font_px < 1 || xs == NULL || ys == NULL || ws == NULL || hs == NULL ||
        labels == NULL) {
        return;
    }
    g_dim = 0;
    float scale = stbtt_ScaleForPixelHeight(&g_font, (float)font_px);
    int ascent = 0;
    int descent = 0;
    int line_gap = 0;
    stbtt_GetFontVMetrics(&g_font, &ascent, &descent, &line_gap);
    int line_h = (int)((ascent - descent + line_gap) * scale + 0.999f);
    int baseline = (int)(ascent * scale + 0.5f);
    if (line_h < 1) {
        line_h = 1;
    }
    int ir, ig, ib;
    slot_rgb(SLOT_INK, &ir, &ig, &ib);
    for (int i = 0; i < n; i++) {
        int x0 = xs[i];
        int y0 = ys[i];
        int cell_w = ws[i];
        int cell_h = hs[i];
        if (cell_w <= 0 || cell_h <= 0) {
            continue;
        }
        if (vertical) {
            for (int row = 0; row < cell_h; row++) {
                blend(px, width, height, x0 + cell_w - 1, y0 + row, 255, ir, ig, ib);
            }
        } else {
            for (int col = 0; col < cell_w; col++) {
                blend(px, width, height, x0 + col, y0, 255, ir, ig, ib);
            }
        }
        const char *s = labels[i] != NULL ? labels[i] : "";
        int x = x0 + font_px / 3;
        int y = y0 + (cell_h - line_h) / 2;
        if (y < y0) {
            y = y0;
        }
        for (const unsigned char *p = (const unsigned char *)s; *p != '\0'; p++) {
            int ax = 0;
            int lsb = 0;
            stbtt_GetCodepointHMetrics(&g_font, *p, &ax, &lsb);
            int advance = (int)((float)ax * scale + 0.5f);
            if (advance < 1) {
                advance = 1;
            }
            if (x + advance > x0 + cell_w) {
                break;
            }
            int gx0, gy0, gx1, gy1;
            stbtt_GetCodepointBitmapBox(&g_font, *p, scale, scale, &gx0, &gy0, &gx1, &gy1);
            int gw = gx1 - gx0;
            int gh = gy1 - gy0;
            if (gw > 0 && gh > 0 && gw <= 128 && gh <= 128) {
                unsigned char bitmap[128 * 128];
                stbtt_MakeCodepointBitmap(&g_font, bitmap, gw, gh, gw, scale, scale, *p);
                for (int by = 0; by < gh; by++) {
                    for (int bx = 0; bx < gw; bx++) {
                        int xx = x + gx0 + bx;
                        int yy = y + baseline + gy0 + by;
                        if (xx < x0 || yy < y0 || xx >= x0 + cell_w || yy >= y0 + cell_h) {
                            continue;
                        }
                        blend(px, width, height, xx, yy, bitmap[by * gw + bx], ir, ig, ib);
                    }
                }
            }
            x += advance;
        }
    }
}
