#include "page.h"

#include "utf8.h"

#include <limits.h>
#include <math.h>
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
static int g_line_hl;
static int g_view_x;
static int g_view_w;
static int g_source;
static int g_readonly;
static int g_editing;
static int g_theme;
static int g_dim;
static int g_seek;
static int g_seek_x;
static int g_seek_row;
static int g_seek_dist;
static size_t g_seek_at;
static uint64_t g_want;
static uint64_t g_span;
static int g_scroll_jump;
static float g_scroll_frac;
static int g_follow = 1;
static int g_bar_x;
static int g_bar_y;
static int g_bar_h;
static int g_sel_ok;
static int g_sel_lo_ok;
static int g_sel_hi_ok;
static int g_sel_x0;
static int g_sel_y0;
static int g_sel_x1;
static int g_sel_y1;
static int g_sel_h;
static int g_pad;
static int g_line_h = 1;
static int g_drag_px;
static int g_words;
static int g_lines;

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
     {42, 44, 50},
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

void page_set_line(int on) {
    g_line_hl = on ? 1 : 0;
}

int page_line(void) {
    return g_line_hl;
}

void page_set_source(int on) {
    g_source = on ? 1 : 0;
    if (g_source) {
        g_readonly = 0;
        g_editing = 1;
    }
}

void page_set_readonly(int on) {
    g_readonly = on ? 1 : 0;
    if (g_readonly) {
        g_source = 0;
        g_editing = 0;
    } else {
        g_editing = 1;
    }
}

int page_readonly(void) {
    return g_readonly;
}

void page_set_editing(int on) {
    g_editing = on ? 1 : 0;
}

int page_editing(void) {
    return g_editing;
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

int page_words(void) {
    return g_words;
}

int page_lines(void) {
    return g_lines;
}

static int ascii_fold(unsigned char c) {
    if (c >= 'A' && c <= 'Z') {
        return c - 'A' + 'a';
    }
    return c;
}

size_t page_find(const char *text, size_t len, const char *needle, size_t needle_len, size_t from, int dir) {
    if (text == NULL) {
        text = "";
        len = 0;
    }
    if (needle == NULL || needle_len == 0 || needle_len > len) {
        return len;
    }
    if (from > len) {
        from = len;
    }
    if (dir < 0) {
        size_t before = len;
        size_t any = len;
        for (size_t i = 0; i < len; i++) {
            unsigned char lead = (unsigned char)text[i];
            if ((lead & 0xc0) == 0x80) {
                continue;
            }
            if (needle_len > len - i) {
                break;
            }
            size_t k = 0;
            for (; k < needle_len; k++) {
                if (ascii_fold((unsigned char)text[i + k]) != ascii_fold((unsigned char)needle[k])) {
                    break;
                }
            }
            if (k != needle_len) {
                continue;
            }
            any = i;
            if (i < from) {
                before = i;
            }
        }
        return before != len ? before : any;
    }
    for (int pass = 0; pass < 2; pass++) {
        size_t begin = pass == 0 ? from : 0;
        size_t end = pass == 0 ? len : from;
        for (size_t i = begin; i < end; i++) {
            unsigned char lead = (unsigned char)text[i];
            if ((lead & 0xc0) == 0x80) {
                continue;
            }
            if (needle_len > len - i) {
                break;
            }
            size_t k = 0;
            for (; k < needle_len; k++) {
                if (ascii_fold((unsigned char)text[i + k]) != ascii_fold((unsigned char)needle[k])) {
                    break;
                }
            }
            if (k == needle_len) {
                return i;
            }
        }
    }
    return len;
}

static int is_heading(const char *s, size_t n);
static int is_setext(const char *s, size_t n);
static int is_fence(const char *s, size_t n);
static int atx_level(const char *s, size_t n);

size_t page_heading(const char *text, size_t len, size_t from, int dir) {
    if (text == NULL) {
        text = "";
        len = 0;
    }
    if (from > len) {
        from = len;
    }
    size_t best = from;
    int found = 0;
    size_t i = 0;
    while (i <= len) {
        size_t off = i;
        while (i < len && text[i] != '\n') {
            i++;
        }
        size_t n = i - off;
        int head = is_heading(text + off, n);
        if (!head && n > 0 && i < len && text[i] == '\n') {
            size_t a = i + 1;
            size_t b = a;
            while (b < len && text[b] != '\n') {
                b++;
            }
            if (is_setext(text + a, b - a)) {
                head = 1;
            }
        }
        if (head && off != from) {
            if (dir > 0 && off > from && (!found || off < best)) {
                best = off;
                found = 1;
            }
            if (dir < 0 && off < from && (!found || off > best)) {
                best = off;
                found = 1;
            }
        }
        if (dir > 0 && found) {
            break;
        }
        if (i < len && text[i] == '\n') {
            i++;
        } else {
            break;
        }
    }
    return best;
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
    int margin = font_px;
    if (margin < 12) {
        margin = 12;
    }
    return margin;
}

/* A sixth of the font line, so the page has room between rows. */
static int with_lead(int line_h) {
    if (line_h < 1) {
        return 1;
    }
    int lead = line_h / 6;
    if (lead < 1) {
        lead = 1;
    }
    if (line_h > INT_MAX - lead) {
        return line_h;
    }
    return line_h + lead;
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

/* A wash under glyphs. Coverage stays low so ink drawn afterwards stays readable. */
static void blend_rect(uint8_t *px, int width, int height, int x, int y, int w, int h, int r, int g, int b,
                       int coverage) {
    if (px == NULL || w <= 0 || h <= 0 || coverage <= 0) {
        return;
    }
    if (coverage > 255) {
        coverage = 255;
    }
    for (int row = 0; row < h; row++) {
        int yy = y + row;
        if (yy < 0 || yy >= height) {
            continue;
        }
        for (int col = 0; col < w; col++) {
            blend(px, width, height, x + col, yy, coverage, r, g, b);
        }
    }
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
       KIND_STRIKE = 7, KIND_RULE = 8, KIND_TABLE = 9, KIND_IMAGE = 10, KIND_SKIP = 11, KIND_STRONG = 12 };

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

/* 1 through 6 for an ATX heading, otherwise 0. */
static int atx_level(const char *s, size_t n) {
    size_t i = 0;
    while (i < n && i < 3 && s[i] == ' ') {
        i++;
    }
    if (i >= n || s[i] != '#') {
        return 0;
    }
    int level = 0;
    while (i < n && s[i] == '#' && level < 6) {
        level++;
        i++;
    }
    if (level == 0 || (i < n && s[i] != ' ' && s[i] != '\t')) {
        return 0;
    }
    return level;
}

/* A rendered single-hash heading uses two body rows and a larger face.
   A source line stays body size so the marks do not jump. */
static void head_face(const char *text, size_t off, size_t content, int source, int line_h, float scale, int *use_h,
                      float *use_scale, int *slots) {
    *use_h = line_h < 1 ? 1 : line_h;
    *use_scale = scale;
    *slots = 1;
    if (source) {
        return;
    }
    if (atx_level(text + off, content) != 1) {
        return;
    }
    if (*use_h <= INT_MAX / 2) {
        *use_h *= 2;
    }
    *use_scale = scale * 1.5f;
    *slots = 2;
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

static int line_visible(const char *s, size_t n) {
    for (size_t k = 0; k < n; k++) {
        if (s[k] != ' ' && s[k] != '\t') {
            return 1;
        }
    }
    return 0;
}

static int setext_level(const char *s, size_t n) {
    if (!is_setext(s, n)) {
        return 0;
    }
    size_t i = 0;
    while (i < n && i < 3 && s[i] == ' ') {
        i++;
    }
    if (i < n && s[i] == '=') {
        return 1;
    }
    return 2;
}

/* Copy the heading words into title. Stops on a code-point boundary. An empty heading keeps its marks. */
static void head_store(struct PageHead *head, size_t at, int level, const char *s, size_t n, int atx) {
    head->at = at;
    if (level < 1) {
        level = 1;
    }
    if (level > 6) {
        level = 6;
    }
    head->level = level;
    head->title[0] = '\0';
    size_t i = 0;
    if (atx) {
        while (i < n && i < 3 && s[i] == ' ') {
            i++;
        }
        while (i < n && s[i] == '#') {
            i++;
        }
        if (i < n && (s[i] == ' ' || s[i] == '\t')) {
            i++;
        }
    }
    size_t end = n;
    while (end > i && (s[end - 1] == ' ' || s[end - 1] == '\t')) {
        end--;
    }
    size_t hash = end;
    while (hash > i && s[hash - 1] == '#') {
        hash--;
    }
    if (hash < end && hash > i && (s[hash - 1] == ' ' || s[hash - 1] == '\t')) {
        end = hash;
        while (end > i && (s[end - 1] == ' ' || s[end - 1] == '\t')) {
            end--;
        }
    }
    size_t o = 0;
    while (i < end) {
        unsigned cp = 0;
        int k = utf8_decode(s + i, end - i, &cp);
        if (k < 1) {
            break;
        }
        if ((size_t)k > (PAGE_HEAD_TITLE - 1) - o) {
            break;
        }
        memcpy(head->title + o, s + i, (size_t)k);
        o += (size_t)k;
        i += (size_t)k;
    }
    head->title[o] = '\0';
    if (o == 0) {
        int marks = level;
        if (marks > PAGE_HEAD_TITLE - 1) {
            marks = PAGE_HEAD_TITLE - 1;
        }
        for (int m = 0; m < marks; m++) {
            head->title[m] = '#';
        }
        head->title[marks] = '\0';
    }
}

int page_heads(const char *text, size_t len, struct PageHead *heads, int cap) {
    if (heads == NULL || cap <= 0) {
        return 0;
    }
    if (cap > PAGE_HEADS_MAX) {
        cap = PAGE_HEADS_MAX;
    }
    if (text == NULL) {
        text = "";
        len = 0;
    }
    int count = 0;
    int fence = 0;
    size_t i = 0;
    while (i <= len && count < cap) {
        size_t off = i;
        while (i < len && text[i] != '\n') {
            i++;
        }
        size_t n = i - off;
        int marker = is_fence(text + off, n);
        if (fence != 0) {
            if (marker == fence) {
                fence = 0;
            }
        } else if (marker != 0) {
            fence = marker;
        } else {
            int level = atx_level(text + off, n);
            if (level > 0) {
                head_store(&heads[count], off, level, text + off, n, 1);
                count++;
            } else if (line_visible(text + off, n) && i < len && text[i] == '\n') {
                size_t a = i + 1;
                size_t b = a;
                while (b < len && text[b] != '\n') {
                    b++;
                }
                int setext = setext_level(text + a, b - a);
                if (setext > 0) {
                    head_store(&heads[count], off, setext, text + off, n, 0);
                    count++;
                }
            }
        }
        if (i < len && text[i] == '\n') {
            i++;
        } else {
            break;
        }
    }
    return count;
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

static int md_punct(unsigned char c) {
    if (c < 33 || c > 126) {
        return 0;
    }
    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
        return 0;
    }
    return 1;
}

/* A backslash hides itself and keeps the next punctuation literal. Returns the index after the pair, or at. */
static size_t escaped_punct(const char *s, size_t n, size_t at) {
    if (at + 1 < n && s[at] == '\\' && md_punct((unsigned char)s[at + 1])) {
        return at + 2;
    }
    return at;
}

enum { REF_MAX = 64, REF_LABEL = 48 };

struct Ref {
    char label[REF_LABEL];
    unsigned char n;
};

static struct Ref g_refs[REF_MAX];
static int g_ref_n;

static size_t norm_label(const char *s, size_t n, char *out, size_t cap) {
    size_t o = 0;
    int gap = 1;
    if (cap == 0) {
        return 0;
    }
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == ' ' || c == '\t') {
            if (gap || o == 0) {
                continue;
            }
            if (o + 1 >= cap) {
                break;
            }
            out[o++] = ' ';
            gap = 1;
            continue;
        }
        gap = 0;
        if (c >= 'A' && c <= 'Z') {
            c = (unsigned char)(c - 'A' + 'a');
        }
        if (o + 1 >= cap) {
            break;
        }
        out[o++] = (char)c;
    }
    while (o > 0 && out[o - 1] == ' ') {
        o--;
    }
    out[o] = '\0';
    return o;
}

static int ref_has(const char *s, size_t n) {
    char key[REF_LABEL];
    size_t kn = norm_label(s, n, key, sizeof key);
    if (kn == 0) {
        return 0;
    }
    for (int i = 0; i < g_ref_n; i++) {
        if (g_refs[i].n == kn && memcmp(g_refs[i].label, key, kn) == 0) {
            return 1;
        }
    }
    return 0;
}

/* A definition is [label]: destination, with at most three leading spaces. Returns the destination start, or 0. */
static size_t definition_at(const char *s, size_t n, char *label, size_t *label_n) {
    size_t i = 0;
    while (i < n && i < 3 && s[i] == ' ') {
        i++;
    }
    if (i >= n || s[i] != '[') {
        return 0;
    }
    size_t lab = ++i;
    while (i < n && s[i] != ']' && s[i] != '[') {
        i++;
    }
    if (i >= n || i == lab || s[i] != ']' || i + 1 >= n || s[i + 1] != ':') {
        return 0;
    }
    size_t ln = norm_label(s + lab, i - lab, label, REF_LABEL);
    if (ln == 0) {
        return 0;
    }
    i += 2;
    while (i < n && (s[i] == ' ' || s[i] == '\t')) {
        i++;
    }
    if (i >= n || s[i] == '<' ) {
        /* An angle destination is still a destination. */
    }
    if (i >= n) {
        return 0;
    }
    *label_n = ln;
    return i;
}

static void gather_refs(const char *text, size_t len) {
    g_ref_n = 0;
    if (text == NULL) {
        return;
    }
    int fence = 0;
    size_t i = 0;
    while (i < len && g_ref_n < REF_MAX) {
        size_t off = i;
        while (i < len && text[i] != '\n') {
            i++;
        }
        size_t n = i - off;
        int marker = is_fence(text + off, n);
        if (marker != 0) {
            fence = fence == marker ? 0 : (fence == 0 ? marker : fence);
        } else if (fence == 0) {
            char label[REF_LABEL];
            size_t ln = 0;
            if (definition_at(text + off, n, label, &ln) != 0 && ln < REF_LABEL) {
                int seen = 0;
                for (int r = 0; r < g_ref_n; r++) {
                    if (g_refs[r].n == ln && memcmp(g_refs[r].label, label, ln) == 0) {
                        seen = 1;
                        break;
                    }
                }
                if (!seen) {
                    memcpy(g_refs[g_ref_n].label, label, ln);
                    g_refs[g_ref_n].label[ln] = '\0';
                    g_refs[g_ref_n].n = (unsigned char)ln;
                    g_ref_n++;
                }
            }
        }
        if (i < len && text[i] == '\n') {
            i++;
        }
    }
}

static int mark_definition(const char *s, size_t n, uint8_t *mark) {
    char label[REF_LABEL];
    size_t ln = 0;
    size_t url = definition_at(s, n, label, &ln);
    if (url == 0) {
        return 0;
    }
    size_t end = url;
    while (end < n && s[end] != ' ' && s[end] != '\t') {
        end++;
    }
    if (end == url) {
        return 0;
    }
    for (size_t k = 0; k < url && k < n; k++) {
        mark[k] = KIND_SKIP;
    }
    for (size_t k = url; k < end && k < n; k++) {
        mark[k] = KIND_LINK;
    }
    for (size_t k = end; k < n; k++) {
        mark[k] = KIND_SKIP;
    }
    return 1;
}

/* shortcut: emphasis is a same-line * or _ pair, not CommonMark flanking rules.
   A backslash before punctuation is literal. Inline marks are skipped on a line longer than 256 KiB. */
static void mark_inline(const char *s, size_t n, uint8_t *mark) {
    memset(mark, KIND_BODY, n);
    if (mark_definition(s, n, mark)) {
        return;
    }
    size_t i = 0;
    while (i < n) {
        if (s[i] == '`') {
            size_t j = i + 1;
            while (j < n && s[j] != '`') {
                j++;
            }
            if (j < n) {
                mark[i] = KIND_SKIP;
                for (size_t k = i + 1; k < j; k++) {
                    mark[k] = KIND_CODE;
                }
                mark[j] = KIND_SKIP;
                i = j + 1;
                continue;
            }
        }
        size_t esc = escaped_punct(s, n, i);
        if (esc != i) {
            mark[i] = KIND_SKIP;
            i = esc;
            continue;
        }
        if (s[i] == '<' ) {
            size_t j = i + 1;
            int autolink = 0;
            if (j + 8 <= n && (memcmp(s + j, "https://", 8) == 0 || (j + 7 <= n && memcmp(s + j, "http://", 7) == 0))) {
                while (j < n && s[j] != '>' && s[j] != ' ' && s[j] != '<') {
                    j++;
                }
                autolink = j < n && s[j] == '>' && j > i + 8;
            } else {
                size_t at = 0;
                int dot = 0;
                while (j < n && s[j] != '>' && s[j] != ' ' && s[j] != '<') {
                    if (s[j] == '@') {
                        at++;
                    }
                    if (at == 1 && s[j] == '.') {
                        dot = 1;
                    }
                    j++;
                }
                autolink = at == 1 && dot && j < n && s[j] == '>' && j > i + 3;
            }
            if (autolink) {
                mark[i] = KIND_SKIP;
                for (size_t k = i + 1; k < j; k++) {
                    mark[k] = KIND_LINK;
                }
                mark[j] = KIND_SKIP;
                i = j + 1;
                continue;
            }
        }
        if (s[i] == '~' && i + 1 < n && s[i + 1] == '~') {
            size_t j = i + 2;
            while (j + 1 < n && !(s[j] == '~' && s[j + 1] == '~')) {
                size_t skip = escaped_punct(s, n, j);
                j = skip == j ? j + 1 : skip;
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
                    mark[i] = KIND_SKIP;
                    for (size_t k = i + 1; k < bracket; k++) {
                        mark[k] = KIND_LINK;
                    }
                    for (size_t k = bracket; k <= end; k++) {
                        mark[k] = KIND_SKIP;
                    }
                    i = end + 1;
                    continue;
                }
            }
            if (bracket < n && s[bracket] == ']' && bracket + 1 < n && s[bracket + 1] == '[') {
                size_t id0 = bracket + 2;
                size_t id1 = id0;
                while (id1 < n && s[id1] != ']' && s[id1] != '\n') {
                    id1++;
                }
                if (id1 < n && s[id1] == ']') {
                    const char *lab = id1 == id0 ? s + i + 1 : s + id0;
                    size_t ln = id1 == id0 ? bracket - (i + 1) : id1 - id0;
                    if (ref_has(lab, ln)) {
                        mark[i] = KIND_SKIP;
                        for (size_t k = i + 1; k < bracket; k++) {
                            mark[k] = KIND_LINK;
                        }
                        for (size_t k = bracket; k <= id1; k++) {
                            mark[k] = KIND_SKIP;
                        }
                        i = id1 + 1;
                        continue;
                    }
                }
            } else if (bracket < n && s[bracket] == ']' && ref_has(s + i + 1, bracket - (i + 1))) {
                mark[i] = KIND_SKIP;
                for (size_t k = i + 1; k < bracket; k++) {
                    mark[k] = KIND_LINK;
                }
                mark[bracket] = KIND_SKIP;
                i = bracket + 1;
                continue;
            }
        }
        if ((s[i] == '*' || s[i] == '_') && i + 1 < n) {
            int strong = s[i + 1] == s[i];
            char marker = s[i];
            size_t j = i + (strong ? 2 : 1);
            int found = 0;
            while (j < n) {
                size_t skip = escaped_punct(s, n, j);
                if (skip != j) {
                    j = skip;
                    continue;
                }
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
                if (end < n) {
                    mark[i] = KIND_SKIP;
                    if (strong && i + 1 < n) {
                        mark[i + 1] = KIND_SKIP;
                    }
                    size_t word = i + (strong ? (size_t)2 : (size_t)1);
                    uint8_t ink = (uint8_t)(strong ? KIND_STRONG : KIND_EM);
                    for (size_t k = word; k < j && k < n;) {
                        size_t skip = escaped_punct(s, n, k);
                        if (skip != k && skip <= j) {
                            mark[k] = KIND_SKIP;
                            if (k + 1 < j) {
                                mark[k + 1] = ink;
                            }
                            k = skip;
                            continue;
                        }
                        mark[k] = ink;
                        k++;
                    }
                    mark[j] = KIND_SKIP;
                    if (strong && j + 1 < n) {
                        mark[j + 1] = KIND_SKIP;
                    }
                    i = end + 1;
                    continue;
                }
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
            /* 4: paint the digits. A bullet square would hide the count. */
            *list = 4;
            any = 1;
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
static int g_measures;

int page_measures(void) {
    return g_measures;
}

static void note_sel(size_t at, int x, uint32_t row, uint64_t row0, int line_h, uint64_t scroll, int origin_y,
                     size_t sel_lo, size_t sel_hi);

static void paint_caret_band(uint8_t *px, int width, int height, uint32_t row, uint64_t row0, int line_h,
                             uint64_t scroll, int origin_y) {
    if (!g_line_hl || px == NULL || g_view_w < 1 || line_h < 1) {
        return;
    }
    uint64_t row_y = 0;
    if (add_rows(row0, row, line_h, &row_y) != 0) {
        return;
    }
    int sy = screen_row(row_y, scroll, origin_y);
    if (sy == INT_MIN) {
        return;
    }
    int sr = 0;
    int sg = 0;
    int sb = 0;
    slot_rgb(SLOT_SEL, &sr, &sg, &sb);
    /* 72/255 of the selection color. Glyphs are drawn after this, so the letters stay visible. */
    blend_rect(px, width, height, g_view_x, sy, g_view_w, line_h, sr, sg, sb, 72);
}

static uint32_t layout_line(uint8_t *px, int width, int height, const char *text, size_t off, size_t len,
                            size_t caret, int origin_x, int max_x, int origin_y, int line_h, int baseline,
                            int space_px, float scale, int draw, uint64_t row0, uint64_t scroll, int view_h,
                            size_t sel_lo, size_t sel_hi, int source, const uint8_t *mark, int uniform,
                            int *caret_x, int *caret_row) {
    if (!draw) {
        g_measures++;
    }
    int x = origin_x;
    int prev = 0;
    uint32_t row = 0;
    size_t i = off;
    size_t end = off + len;
    size_t hide = 0;
    if (!source && len > 0) {
        if (uniform == KIND_CODE && is_fence(text + off, len)) {
            hide = len;
        } else if (uniform == KIND_CODE && text[off] == '\t') {
            hide = 1;
        } else if (uniform == KIND_CODE && len >= 4 && text[off] == ' ' && text[off + 1] == ' ' && text[off + 2] == ' ' &&
                   text[off + 3] == ' ') {
            hide = 4;
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
                        } else if (list != 4) {
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
                    if (list != 4 && x <= INT_MAX - side - space_px) {
                        x += side + space_px;
                    }
                }
            }
        }
    }
    if (caret >= off && caret <= end) {
        paint_caret_band(px, width, height, row, row0, line_h, scroll, origin_y);
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
        note_sel(i, x, row, row0, line_h, scroll, origin_y, sel_lo, sel_hi);
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
            if (caret >= off && caret <= end) {
                paint_caret_band(px, width, height, row, row0, line_h, scroll, origin_y);
            }
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
                /* The regular face has no bold. A second stamp is the strong weight. */
                if (kind == KIND_STRONG && dest_x < INT_MAX) {
                    for (int by = 0; by < gh; by++) {
                        for (int bx = 0; bx < gw; bx++) {
                            blend(px, width, height, dest_x + 1 + bx, dest_y + by, bitmap[by * gw + bx], cr, cg, cb);
                        }
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
    note_sel(off + len, x, row, row0, line_h, scroll, origin_y, sel_lo, sel_hi);
    return row == UINT32_MAX ? UINT32_MAX : row + 1;
}

static int list_mark_at(const char *s, size_t n) {
    if (n >= 2 && (s[0] == '-' || s[0] == '+' || s[0] == '*') && s[1] == ' ') {
        return 1;
    }
    if (n >= 1 && s[0] == '>') {
        return 1;
    }
    size_t d = 0;
    while (d < n && d < 9 && s[d] >= '0' && s[d] <= '9') {
        d++;
    }
    return d > 0 && d + 1 < n && s[d] == '.' && s[d + 1] == ' ';
}

/* Four spaces or a tab, and not a nested list or quote. */
static int is_indented_code(const char *s, size_t n) {
    size_t span = 0;
    if (n > 0 && s[0] == '\t') {
        span = 1;
    } else if (n >= 4 && s[0] == ' ' && s[1] == ' ' && s[2] == ' ' && s[3] == ' ') {
        span = 4;
    }
    if (span == 0 || span > n) {
        return 0;
    }
    return !list_mark_at(s + span, n - span);
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

static struct Index g_keep;
static uint64_t g_keep_hash;
static size_t g_keep_len;
static size_t g_keep_caret;
static int g_keep_ox;
static int g_keep_mx;
static int g_keep_lh;
static int g_keep_sp;
static int g_keep_editing;
static int g_keep_readonly;
static int g_keep_source;
static float g_keep_scale;
static int g_keep_x;
static uint64_t g_keep_y;
static int g_keep_ok;

static uint64_t text_hash(const char *text, size_t n) {
    uint64_t hash = 14695981039346656037ull;
    for (size_t i = 0; i < n; i++) {
        hash ^= (unsigned char)text[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

static int keep_copy(struct Index *idx) {
    struct Line *lines = malloc(g_keep.n * sizeof *lines);
    if (lines == NULL && g_keep.n > 0) {
        return -1;
    }
    if (g_keep.n > 0) {
        memcpy(lines, g_keep.lines, g_keep.n * sizeof *lines);
    }
    idx->lines = lines;
    idx->n = g_keep.n;
    idx->cap = g_keep.n;
    idx->caret_x = g_keep_x;
    idx->caret_y = g_keep_y;
    return 0;
}

static void keep_store(const struct Index *idx, const char *text, size_t text_len, size_t caret, int origin_x, int max_x,
                       int line_h, int space_px, float scale) {
    struct Line *lines = malloc(idx->n * sizeof *lines);
    if (lines == NULL && idx->n > 0) {
        g_keep_ok = 0;
        return;
    }
    if (idx->n > 0) {
        memcpy(lines, idx->lines, idx->n * sizeof *lines);
    }
    free(g_keep.lines);
    g_keep.lines = lines;
    g_keep.n = idx->n;
    g_keep.cap = idx->n;
    g_keep_hash = text_hash(text, text_len);
    g_keep_len = text_len;
    g_keep_caret = caret;
    g_keep_ox = origin_x;
    g_keep_mx = max_x;
    g_keep_lh = line_h;
    g_keep_sp = space_px;
    g_keep_scale = scale;
    g_keep_editing = g_editing;
    g_keep_readonly = g_readonly;
    g_keep_source = g_source;
    g_keep_x = idx->caret_x;
    g_keep_y = idx->caret_y;
    g_keep_ok = 1;
}

/* Row count changes with the caret only for a rendered level-1 heading, which takes two body rows.
   Every other line was measured as source, so a caret move does not rewrap it. */
static int heading_slots(const char *text, struct Line line, size_t caret) {
    int on = caret >= line.off && caret <= line.off + line.content;
    int source = !g_readonly && (g_source || (g_editing && on));
    if (source || atx_level(text + line.off, line.content) != 1) {
        return 1;
    }
    return 2;
}

static void retarget_rows(const char *text, struct Line *line, size_t old_caret, size_t caret) {
    int old_slots = heading_slots(text, *line, old_caret);
    int slots = heading_slots(text, *line, caret);
    if (old_slots == slots) {
        return;
    }
    uint32_t rows = line->rows;
    if (old_slots == 2 && slots == 1 && rows >= 2) {
        rows /= 2;
    } else if (old_slots == 1 && slots == 2 && rows <= UINT32_MAX / 2) {
        rows *= 2;
    }
    line->rows = rows == 0 ? 1 : rows;
}

static int keep_geometry(const char *text, size_t text_len, int origin_x, int max_x, int line_h, int space_px,
                         float scale) {
    return g_keep_ok && g_keep.n > 0 && text != NULL && g_keep_len == text_len && g_keep_ox == origin_x &&
           g_keep_mx == max_x && g_keep_lh == line_h && g_keep_sp == space_px && g_keep_scale == scale &&
           g_keep_editing == g_editing && g_keep_readonly == g_readonly && g_keep_source == g_source &&
           text_hash(text, text_len) == g_keep_hash;
}

/* Move the caret on a stored index. One line is measured for the caret column. */
static int keep_move(struct Index *idx, const char *text, size_t text_len, size_t caret, int origin_x, int max_x,
                     int line_h, int space_px, float scale) {
    if (!keep_geometry(text, text_len, origin_x, max_x, line_h, space_px, scale) || g_keep_caret == caret) {
        return -1;
    }
    for (size_t i = 0; i < g_keep.n; i++) {
        struct Line *line = &g_keep.lines[i];
        size_t end = line->off + line->content;
        int touch = (g_keep_caret >= line->off && g_keep_caret <= end) || (caret >= line->off && caret <= end);
        if (touch) {
            retarget_rows(text, line, g_keep_caret, caret);
        }
    }
    if (keep_copy(idx) != 0) {
        return -1;
    }
    uint64_t y = 0;
    int found = 0;
    for (size_t i = 0; i < idx->n; i++) {
        struct Line line = idx->lines[i];
        size_t end = line.off + line.content;
        if (caret < line.off || caret > end) {
            uint64_t next = 0;
            if (add_rows(y, line.rows, line_h, &next) != 0) {
                free(idx->lines);
                idx->lines = NULL;
                return -1;
            }
            y = next;
            continue;
        }
        int source_line = !g_readonly && (g_source || g_editing);
        int use_h = line_h;
        float use_scale = scale;
        int slots = 1;
        int cx = origin_x;
        int crow = 0;
        head_face(text, line.off, line.content, source_line, line_h, scale, &use_h, &use_scale, &slots);
        layout_line(NULL, 0, 0, text, line.off, line.content, caret, origin_x, max_x, 0, use_h, 0, space_px, use_scale,
                    0, y, 0, 1, 0, 0, 1, NULL, KIND_BODY, &cx, &crow);
        uint64_t caret_y = 0;
        uint32_t crow_u = crow < 0 ? 0 : (uint32_t)crow;
        if (add_rows(y, crow_u, line_h, &caret_y) != 0) {
            caret_y = UINT64_MAX;
        }
        idx->caret_x = cx;
        idx->caret_y = caret_y;
        g_keep_x = cx;
        g_keep_y = caret_y;
        g_keep_caret = caret;
        found = 1;
        break;
    }
    if (!found) {
        free(idx->lines);
        idx->lines = NULL;
        idx->n = 0;
        return -1;
    }
    return 0;
}

/* The line index is reused while the note, the width, and the caret stay put.
   A caret move reuses the stored rows and measures only the caret line. */
static int build_index(struct Index *idx, const char *text, size_t text_len, size_t caret, int origin_x, int max_x,
                       int line_h, int space_px, float scale) {
    if (keep_geometry(text, text_len, origin_x, max_x, line_h, space_px, scale) && g_keep_caret == caret) {
        return keep_copy(idx);
    }
    if (keep_move(idx, text, text_len, caret, origin_x, max_x, line_h, space_px, scale) == 0) {
        return 0;
    }
    idx->caret_x = origin_x;
    idx->caret_y = 0;
    g_words = 0;
    g_lines = 0;
    int fence = 0;
    int indent_run = 0;
    int next_underline = 0;
    size_t i = 0;
    uint64_t y = 0;
    while (i <= text_len) {
        size_t off = i;
        int in_word = 0;
        while (i < text_len && text[i] != '\n') {
            unsigned char c = (unsigned char)text[i];
            int wordish = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c >= 128;
            if (!wordish) {
                in_word = 0;
            } else if (!in_word) {
                g_words++;
                in_word = 1;
            }
            i++;
        }
        size_t content = i - off;
        g_lines++;
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
            if (fence != 0) {
                indent_run = 0;
            } else if (kind == KIND_BODY && is_indented_code(text + off, content)) {
                kind = KIND_CODE;
                indent_run = 1;
            } else if (kind == KIND_BODY && content == 0 && indent_run && i < text_len && text[i] == '\n') {
                size_t a = i + 1;
                size_t b = a;
                while (b < text_len && text[b] != '\n') {
                    b++;
                }
                if (is_indented_code(text + a, b - a)) {
                    kind = KIND_CODE;
                } else {
                    indent_run = 0;
                }
            } else {
                indent_run = 0;
            }
        }
        int cx = origin_x;
        int crow = 0;
        int use_h = line_h;
        float use_scale = scale;
        int slots = 1;
        int on_caret = caret >= off && caret <= off + content;
        int source_line = !g_readonly && (g_source || (g_editing && on_caret));
        head_face(text, off, content, source_line, line_h, scale, &use_h, &use_scale, &slots);
        uint32_t rows = layout_line(NULL, 0, 0, text, off, content, caret, origin_x, max_x, 0, use_h, 0, space_px,
                                    use_scale, 0, y, 0, 1, 0, 0, 1, NULL, KIND_BODY, &cx, &crow);
        if (rows == 0) {
            rows = 1;
        }
        if (slots == 2 && rows <= UINT32_MAX / 2) {
            rows *= 2;
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
    keep_store(idx, text, text_len, caret, origin_x, max_x, line_h, space_px, scale);
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
    g_span = total;
    if (g_scroll_jump) {
        float frac = g_scroll_frac;
        if (frac < 0.f) {
            frac = 0.f;
        }
        if (frac > 1.f) {
            frac = 1.f;
        }
        g_want = (uint64_t)((double)max_scroll * (double)frac + 0.5);
        g_scroll_jump = 0;
        g_follow = 0;
    }
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

static int g_glide_level = 2;

void page_set_glide(int level) {
    if (level < 0) {
        level = 0;
    }
    if (level > 4) {
        level = 4;
    }
    g_glide_level = level;
}

int page_glide_level(void) {
    return g_glide_level;
}

float page_glide_gain(void) {
    static const float k_gain[] = {0.f, 0.75f, 1.f, 1.15f, 1.35f};
    return k_gain[g_glide_level];
}

/* Android SplineOverScroller, the curve Chromium uses for a WebView fling.
   Friction is ViewConfiguration's 0.015 on medium. Short stops sooner. Far carries farther. */
enum { FLING_SAMPLES = 100 };

static float g_spline_pos[FLING_SAMPLES + 1];
static int g_spline_ready;
static float g_fling_dist;
static int g_fling_dur;
static float g_fling_done;

static void fling_prepare(void) {
    if (g_spline_ready) {
        return;
    }
    const float p1 = 0.175f;
    const float p2 = 0.35f;
    const float start_tension = 0.5f;
    float x_min = 0.f;
    for (int i = 0; i < FLING_SAMPLES; i++) {
        float alpha = (float)i / (float)FLING_SAMPLES;
        float x_max = 1.f;
        float x = 0.5f;
        float coef = 0.f;
        for (int n = 0; n < 32; n++) {
            x = (x_min + x_max) * 0.5f;
            coef = 3.f * x * (1.f - x);
            float tx = coef * ((1.f - x) * p1 + x * p2) + x * x * x;
            if (tx > alpha) {
                x_max = x;
            } else {
                x_min = x;
            }
        }
        g_spline_pos[i] = coef * ((1.f - x) * start_tension + x) + x * x * x;
    }
    g_spline_pos[FLING_SAMPLES] = 1.f;
    g_spline_ready = 1;
}

void page_fling_cancel(void) {
    g_fling_dur = 0;
    g_fling_dist = 0.f;
    g_fling_done = 0.f;
}

int page_fling_active(void) {
    return g_fling_dur > 0;
}

int page_fling_arm(float velocity_px_per_s, float dpi) {
    static const float k_friction[] = {0.f, 0.04f, 0.015f, 0.008f, 0.0045f};
    fling_prepare();
    page_fling_cancel();
    if (g_glide_level <= 0 || dpi < 1.f) {
        return 0;
    }
    if (velocity_px_per_s > 30000.f) {
        velocity_px_per_s = 30000.f;
    }
    if (velocity_px_per_s < -30000.f) {
        velocity_px_per_s = -30000.f;
    }
    float speed = fabsf(velocity_px_per_s);
    if (speed < 80.f) {
        return 0;
    }
    float friction = k_friction[g_glide_level];
    float phy = 9.80665f * 39.37f * dpi * 0.84f;
    float denom = friction * phy;
    if (denom < 1.f) {
        return 0;
    }
    float arg = 0.35f * speed / denom;
    if (arg < 0.05f) {
        return 0;
    }
    double l = log((double)arg);
    double decel_m1 = 2.3582018 - 1.0;
    double dist = (double)denom * exp(2.3582018 / decel_m1 * l);
    double dur = 1000.0 * exp(l / decel_m1);
    if (dur < 1.0) {
        dur = 1.0;
    }
    if (dur > 4500.0) {
        dur = 4500.0;
    }
    if (dist > 250000.0) {
        dist = 250000.0;
    }
    g_fling_dist = velocity_px_per_s < 0.f ? (float)-dist : (float)dist;
    g_fling_dur = (int)dur;
    g_fling_done = 0.f;
    return g_fling_dur > 0;
}

float page_fling_at(int elapsed_ms) {
    if (g_fling_dur <= 0) {
        return g_fling_done;
    }
    if (elapsed_ms <= 0) {
        return 0.f;
    }
    if (elapsed_ms >= g_fling_dur) {
        return g_fling_dist;
    }
    float t = (float)elapsed_ms / (float)g_fling_dur;
    float x = t * (float)FLING_SAMPLES;
    int i = (int)x;
    if (i < 0) {
        i = 0;
    }
    if (i >= FLING_SAMPLES) {
        i = FLING_SAMPLES - 1;
    }
    float frac = x - (float)i;
    float coef = g_spline_pos[i] + (g_spline_pos[i + 1] - g_spline_pos[i]) * frac;
    return g_fling_dist * coef;
}

float page_fling_step(int elapsed_ms) {
    float at = page_fling_at(elapsed_ms);
    float delta = at - g_fling_done;
    g_fling_done = at;
    if (elapsed_ms >= g_fling_dur) {
        g_fling_dur = 0;
    }
    return delta;
}

float page_fling_velocity(int elapsed_ms) {
    if (g_fling_dur <= 0 || elapsed_ms >= g_fling_dur) {
        return 0.f;
    }
    int t1 = elapsed_ms + 16;
    if (t1 > g_fling_dur) {
        t1 = g_fling_dur;
    }
    int dt = t1 - elapsed_ms;
    if (dt < 1) {
        return 0.f;
    }
    return (page_fling_at(t1) - page_fling_at(elapsed_ms)) / (float)dt * 1000.f;
}

void page_scroll_place(float frac) {
    g_scroll_frac = frac;
    g_scroll_jump = 1;
    g_follow = 0;
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

int page_drag_kind(int past_slop, int held, int near_caret, int typewriter) {
    if (held) {
        return 2;
    }
    if (!past_slop) {
        return 0;
    }
    if (typewriter || !near_caret) {
        return 1;
    }
    return 2;
}

static void note_sel(size_t at, int x, uint32_t row, uint64_t row0, int line_h, uint64_t scroll, int origin_y,
                     size_t sel_lo, size_t sel_hi) {
    if (sel_hi <= sel_lo || (at != sel_lo && at != sel_hi)) {
        return;
    }
    uint64_t row_y = 0;
    if (add_rows(row0, row, line_h, &row_y) != 0) {
        return;
    }
    int sy = screen_row(row_y, scroll, origin_y);
    if (sy == INT_MIN) {
        return;
    }
    if (at == sel_lo) {
        g_sel_x0 = x;
        g_sel_y0 = sy;
        g_sel_h = line_h;
        g_sel_lo_ok = 1;
    }
    if (at == sel_hi) {
        g_sel_x1 = x;
        g_sel_y1 = sy;
        g_sel_h = line_h;
        g_sel_hi_ok = 1;
    }
    g_sel_ok = g_sel_lo_ok && g_sel_hi_ok;
}

int page_sel_ends(int *x0, int *y0, int *x1, int *y1, int *h) {
    if (!g_sel_ok || x0 == NULL || y0 == NULL || x1 == NULL || y1 == NULL || h == NULL) {
        return -1;
    }
    *x0 = g_sel_x0;
    *y0 = g_sel_y0;
    *x1 = g_sel_x1;
    *y1 = g_sel_y1;
    *h = g_sel_h > 0 ? g_sel_h : 1;
    return 0;
}

int page_sel_edge(int x, int y, int slop) {
    if (!g_sel_ok || slop < 1) {
        return -1;
    }
    int h = g_sel_h > 0 ? g_sel_h : 1;
    int d0x = x - g_sel_x0;
    int d0y = y - (g_sel_y0 + h / 2);
    int d1x = x - g_sel_x1;
    int d1y = y - (g_sel_y1 + h / 2);
    if (d0x < 0) {
        d0x = -d0x;
    }
    if (d0y < 0) {
        d0y = -d0y;
    }
    if (d1x < 0) {
        d1x = -d1x;
    }
    if (d1y < 0) {
        d1y = -d1y;
    }
    int hit0 = d0x <= slop && d0y <= slop + h / 2;
    int hit1 = d1x <= slop && d1y <= slop + h / 2;
    if (hit0 && hit1) {
        return d0x + d0y <= d1x + d1y ? 0 : 1;
    }
    if (hit0) {
        return 0;
    }
    if (hit1) {
        return 1;
    }
    return -1;
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

static size_t unit_of(const struct Index *idx, size_t li) {
    int kind = idx->lines[li].kind;
    if (kind == KIND_CODE || kind == KIND_TABLE) {
        while (li > 0 && idx->lines[li - 1].kind == kind) {
            li--;
        }
    }
    return li;
}

static int find_line(const struct Index *idx, size_t byte, size_t *li) {
    for (size_t i = 0; i < idx->n; i++) {
        size_t end = idx->lines[i].off + idx->lines[i].content;
        if (byte >= idx->lines[i].off && byte <= end) {
            *li = i;
            return 1;
        }
    }
    return 0;
}

/* Normal mode shows marks on the caret line only. Read only shows none. Source shows all. */
static int line_is_source(const struct Index *idx, size_t li, size_t caret_li) {
    (void)idx;
    if (g_readonly) {
        return 0;
    }
    if (g_source) {
        return 1;
    }
    if (!g_editing) {
        return 0;
    }
    return li == caret_li;
}

/* A quiet thumb at the right of the column. Hidden when the note fits. */
static void paint_scrollbar(uint8_t *px, int width, int height, int box_x, int track_y, int box_w, int track_h,
                            uint64_t scroll, uint64_t total) {
    if (px == NULL || track_h < 8 || box_w < 8 || total <= (uint64_t)track_h) {
        return;
    }
    int thumb = (int)(((uint64_t)track_h * (uint64_t)track_h) / total);
    int min_thumb = track_h / 8;
    if (min_thumb < 36) {
        min_thumb = 36;
    }
    if (min_thumb > track_h) {
        min_thumb = track_h;
    }
    if (thumb < min_thumb) {
        thumb = min_thumb;
    }
    if (thumb > track_h) {
        thumb = track_h;
    }
    uint64_t max_scroll = total - (uint64_t)track_h;
    int travel = track_h - thumb;
    int top = 0;
    if (max_scroll > 0 && travel > 0) {
        if (scroll > max_scroll) {
            scroll = max_scroll;
        }
        top = (int)(scroll * (uint64_t)travel / max_scroll);
    }
    int thick = 4;
    int x = box_x + box_w - thick - 4;
    if (x < 0) {
        return;
    }
    int r = 0;
    int g = 0;
    int b = 0;
    slot_rgb(SLOT_INK, &r, &g, &b);
    blend_rect(px, width, height, x, track_y + top, thick, thumb, r, g, b, 110);
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
    gather_refs(text, text_len);
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
    g_view_x = box_x;
    g_view_w = box_w > 0 ? box_w : 0;
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
    int line_h = with_lead((int)line_raw);
    int baseline = (int)base_raw;
    int sax = 0;
    int slsb = 0;
    stbtt_GetCodepointHMetrics(&g_font, ' ', &sax, &slsb);
    int space_px = (int)(sax * scale + 0.5f);
    if (space_px < 1) {
        space_px = 1;
    }

    g_sel_ok = 0;
    g_sel_lo_ok = 0;
    g_sel_hi_ok = 0;
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
        int source = line_is_source(&idx, li, caret_li);
        g_dim = g_focus && !source && li != caret_li;
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
            int use_h = line_h;
            float use_scale = scale;
            int slots = 1;
            head_face(text, line.off, line.content, source, line_h, scale, &use_h, &use_scale, &slots);
            int use_base = baseline;
            if (slots == 2) {
                use_base = (int)((float)ascent * use_scale + 0.5f);
            }
            /* shortcut: a rendered line keeps the source row count. */
            layout_line(px, width, height, text, line.off, line.content, caret, origin_x, max_x, origin_y, use_h,
                        use_base, space_px, use_scale, 1, y, scroll, view_h, sel_lo, sel_hi, source, marks, line.kind,
                        &cx, &crow);
            (void)slots;
            free(mark);
        }
        y = next;
        if (scroll <= UINT64_MAX - (uint64_t)view_h && y > scroll + (uint64_t)view_h + (uint64_t)line_h) {
            break;
        }
    }
    g_dim = 0;
    paint_scrollbar(px, width, height, box_x, origin_y, box_w, view_h, scroll, g_span);
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
    if (g_sel_ok && sel_hi > sel_lo) {
        int grip = line_h / 5;
        if (grip < 3) {
            grip = 3;
        }
        int extra = line_h / 4;
        int cr, cg, cb;
        slot_rgb(SLOT_INK, &cr, &cg, &cb);
        int y0 = g_sel_y0 - extra;
        int y1 = g_sel_y1 - extra;
        int gh = line_h + extra * 2;
        fill_rect(px, width, height, g_sel_x0 - grip / 2, y0, grip, gh, cr, cg, cb);
        fill_rect(px, width, height, g_sel_x1 - grip / 2, y1, grip, gh, cr, cg, cb);
    }
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
    int line_h = with_lead((int)line_raw);
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
    int source = line_is_source(&idx, li, caret_li);
    uint64_t into = doc_y >= line_y ? doc_y - line_y : 0;
    int use_h = line_h;
    float use_scale = scale;
    int slots = 1;
    head_face(text, line.off, line.content, source, line_h, scale, &use_h, &use_scale, &slots);
    (void)slots;
    int hit_row = 0;
    if (use_h > 0 && into / (uint64_t)use_h <= (uint64_t)INT_MAX) {
        hit_row = (int)(into / (uint64_t)use_h);
    }
    g_seek = 1;
    g_seek_x = x;
    g_seek_row = hit_row;
    g_seek_at = line.off;
    g_seek_dist = INT_MAX;
    int cx = 0;
    int crow = 0;
    layout_line(NULL, 0, 0, text, line.off, line.content, caret, origin_x, max_x, origin_y, use_h, 0, space_px,
                use_scale, 0, line_y, scroll, view_h, 0, 0, source, NULL, line.kind, &cx, &crow);
    g_seek = 0;
    size_t at = g_seek_at;
    free(idx.lines);
    if (at > text_len) {
        at = text_len;
    }
    return at;
}

void page_draw_labels(uint8_t *px, int width, int height, int n, const int *xs, const int *ys, const int *ws,
                      const int *hs, const char *const *labels, int vertical, int font_px, int clip_top,
                      int clip_bottom) {
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
        const char *s = labels[i] != NULL ? labels[i] : "";
        if (s[0] == '\0') {
            continue;
        }
        if (vertical) {
            for (int row = 0; row < cell_h; row++) {
                int yy = y0 + row;
                if (yy < clip_top || yy >= clip_bottom) {
                    continue;
                }
                blend(px, width, height, x0 + cell_w - 1, yy, 255, ir, ig, ib);
            }
        } else if (y0 >= clip_top && y0 < clip_bottom) {
            for (int col = 0; col < cell_w; col++) {
                blend(px, width, height, x0 + col, y0, 255, ir, ig, ib);
            }
        }
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
                        if (xx < x0 || yy < y0 || xx >= x0 + cell_w || yy >= y0 + cell_h || yy < clip_top ||
                            yy >= clip_bottom) {
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

void page_draw_menu(uint8_t *px, int width, int height, int x, int y, int side) {
    if (px == NULL || width < 1 || height < 1 || side < 1) {
        return;
    }
    if (side > 1024) {
        side = 1024;
    }
    if (x >= width || y >= height || x < -1024 || y < -1024) {
        return;
    }
    if (x + side <= 0 || y + side <= 0) {
        return;
    }
    if ((x > 0 && side / 2 > INT_MAX - x) || (y > 0 && side / 2 > INT_MAX - y)) {
        return;
    }
    int cx = x + side / 2;
    int cy = y + side / 2;
    int plate = side / 2;
    if (plate < 1) {
        plate = 1;
    }
    int pr, pg, pb, ir, ig, ib;
    slot_rgb(SLOT_PAPER, &pr, &pg, &pb);
    slot_rgb(SLOT_INK, &ir, &ig, &ib);
    int64_t plate2 = (int64_t)plate * (int64_t)plate;
    for (int row = 0; row < side; row++) {
        if (y > 0 && row > INT_MAX - y) {
            break;
        }
        int yy = y + row;
        if (yy < 0 || yy >= height) {
            continue;
        }
        for (int col = 0; col < side; col++) {
            if (x > 0 && col > INT_MAX - x) {
                break;
            }
            int xx = x + col;
            if (xx < 0 || xx >= width) {
                continue;
            }
            int64_t dx64 = (int64_t)xx - (int64_t)cx;
            int64_t dy64 = (int64_t)yy - (int64_t)cy;
            int64_t d2 = dx64 * dx64 + dy64 * dy64;
            if (d2 > plate2) {
                continue;
            }
            uint8_t *p = px + ((size_t)yy * (size_t)width + (size_t)xx) * 4;
            p[0] = (uint8_t)pr;
            p[1] = (uint8_t)pg;
            p[2] = (uint8_t)pb;
            p[3] = 255;
        }
    }
    int thick = side / 9;
    if (thick < 2) {
        thick = 2;
    }
    int span = side / 2;
    if (span < thick) {
        span = thick;
    }
    int gap = thick;
    int total = thick * 3 + gap * 2;
    int left = x + (side - span) / 2;
    int top = y + (side - total) / 2;
    for (int bar = 0; bar < 3; bar++) {
        int by = top + bar * (thick + gap);
        for (int row = 0; row < thick; row++) {
            int yy = by + row;
            if (yy < 0 || yy >= height) {
                continue;
            }
            for (int col = 0; col < span; col++) {
                int xx = left + col;
                if (xx < 0 || xx >= width) {
                    continue;
                }
                uint8_t *p = px + ((size_t)yy * (size_t)width + (size_t)xx) * 4;
                p[0] = (uint8_t)ir;
                p[1] = (uint8_t)ig;
                p[2] = (uint8_t)ib;
                p[3] = 255;
            }
        }
    }
}

int page_same_unit(const char *text, size_t text_len, size_t a, size_t b) {
    if (!g_font_ok) {
        return 0;
    }
    if (text == NULL) {
        text = "";
        text_len = 0;
    }
    if (a > text_len) {
        a = text_len;
    }
    if (b > text_len) {
        b = text_len;
    }
    float scale = stbtt_ScaleForPixelHeight(&g_font, 16.f);
    struct Index idx;
    memset(&idx, 0, sizeof idx);
    if (build_index(&idx, text, text_len, a, 0, 100000, 16, 4, scale) != 0 || idx.n == 0) {
        free(idx.lines);
        return 0;
    }
    size_t la = 0;
    size_t lb = 0;
    int same = find_line(&idx, a, &la) && find_line(&idx, b, &lb) && unit_of(&idx, la) == unit_of(&idx, lb);
    free(idx.lines);
    return same;
}

static void ink_cover(uint8_t *px, int width, int height, int x, int y, float cover, int r, int g, int b) {
    if (cover <= 0.f) {
        return;
    }
    if (cover > 1.f) {
        cover = 1.f;
    }
    blend(px, width, height, x, y, (int)(cover * 255.f + 0.5f), r, g, b);
}

/* A round-capped stroke. Coverage falls off across one pixel, so the edge is not a stair. */
static void stroke_seg(uint8_t *px, int width, int height, float x0, float y0, float x1, float y1, float thick, int r,
                       int g, int b) {
    float dx = x1 - x0;
    float dy = y1 - y0;
    float len2 = dx * dx + dy * dy;
    float rad = thick * 0.5f;
    if (rad < 0.6f) {
        rad = 0.6f;
    }
    float minx = x0 < x1 ? x0 : x1;
    float maxx = x0 > x1 ? x0 : x1;
    float miny = y0 < y1 ? y0 : y1;
    float maxy = y0 > y1 ? y0 : y1;
    int xa = (int)(minx - rad - 1.f);
    int ya = (int)(miny - rad - 1.f);
    int xb = (int)(maxx + rad + 1.f);
    int yb = (int)(maxy + rad + 1.f);
    for (int yy = ya; yy <= yb; yy++) {
        for (int xx = xa; xx <= xb; xx++) {
            float pxn = (float)xx + 0.5f;
            float pyn = (float)yy + 0.5f;
            float t = 0.f;
            if (len2 > 0.01f) {
                t = ((pxn - x0) * dx + (pyn - y0) * dy) / len2;
                if (t < 0.f) {
                    t = 0.f;
                }
                if (t > 1.f) {
                    t = 1.f;
                }
            }
            float qx = x0 + t * dx - pxn;
            float qy = y0 + t * dy - pyn;
            ink_cover(px, width, height, xx, yy, rad + 0.5f - sqrtf(qx * qx + qy * qy), r, g, b);
        }
    }
}

static void stroke_poly(uint8_t *px, int width, int height, float ox, float oy, float side, const float *uv, int n,
                        float thick, int r, int g, int b) {
    for (int i = 0; i + 1 < n; i++) {
        stroke_seg(px, width, height, ox + uv[i * 2] * side, oy + uv[i * 2 + 1] * side, ox + uv[(i + 1) * 2] * side,
                   oy + uv[(i + 1) * 2 + 1] * side, thick, r, g, b);
    }
}

static void stroke_ring(uint8_t *px, int width, int height, float cx, float cy, float rad, float thick, int r, int g,
                        int b) {
    int steps = (int)(rad * 1.5f) + 16;
    if (steps < 16) {
        steps = 16;
    }
    if (steps > 40) {
        steps = 40;
    }
    for (int i = 0; i < steps; i++) {
        float a0 = 6.2831853f * (float)i / (float)steps;
        float a1 = 6.2831853f * (float)(i + 1) / (float)steps;
        stroke_seg(px, width, height, cx + cosf(a0) * rad, cy + sinf(a0) * rad, cx + cosf(a1) * rad, cy + sinf(a1) * rad,
                   thick, r, g, b);
    }
}

static void stroke_box(uint8_t *px, int width, int height, float ox, float oy, float side, float x0, float y0, float x1,
                       float y1, float thick, int r, int g, int b) {
    float xy[] = {x0, y0, x1, y0, x1, y1, x0, y1, x0, y0};
    stroke_poly(px, width, height, ox, oy, side, xy, 5, thick, r, g, b);
}

void page_draw_tool(uint8_t *px, int width, int height, int x, int y, int w, int h, int tool) {
    if (px == NULL || w < 4 || h < 4 || tool < 0 || tool > 11) {
        return;
    }
    int hr = 0;
    int hg = 0;
    int hb = 0;
    slot_rgb(SLOT_RULE, &hr, &hg, &hb);
    blend_rect(px, width, height, x, y, w, 1, hr, hg, hb, 140);
    int room = w < h ? w : h;
    int side = room * 22 / 48;
    if (side < 16) {
        side = 16;
    }
    if (side > room - 4) {
        side = room - 4;
    }
    if (side < 8) {
        side = 8;
    }
    float ox = (float)x + (float)(w - side) * 0.5f;
    float oy = (float)y + (float)(h - side) * 0.5f;
    float s = (float)side;
    float thick = s * 0.09f;
    if (thick < 1.6f) {
        thick = 1.6f;
    }
    int r = 0;
    int g = 0;
    int b = 0;
    slot_rgb(SLOT_INK, &r, &g, &b);
    if (tool == 0) {
        const float stem[] = {0.28f, 0.14f, 0.28f, 0.86f};
        const float top[] = {0.28f, 0.16f, 0.52f, 0.14f, 0.70f, 0.28f, 0.52f, 0.48f, 0.28f, 0.48f};
        const float bot[] = {0.28f, 0.48f, 0.56f, 0.48f, 0.76f, 0.64f, 0.56f, 0.86f, 0.28f, 0.86f};
        stroke_poly(px, width, height, ox, oy, s, stem, 2, thick, r, g, b);
        stroke_poly(px, width, height, ox, oy, s, top, 5, thick, r, g, b);
        stroke_poly(px, width, height, ox, oy, s, bot, 5, thick, r, g, b);
    } else if (tool == 1) {
        const float stem[] = {0.64f, 0.14f, 0.36f, 0.86f};
        const float top[] = {0.46f, 0.16f, 0.76f, 0.16f};
        const float bot[] = {0.24f, 0.84f, 0.54f, 0.84f};
        stroke_poly(px, width, height, ox, oy, s, stem, 2, thick, r, g, b);
        stroke_poly(px, width, height, ox, oy, s, top, 2, thick * 0.85f, r, g, b);
        stroke_poly(px, width, height, ox, oy, s, bot, 2, thick * 0.85f, r, g, b);
    } else if (tool == 2) {
        const float curve[] = {0.70f, 0.22f, 0.36f, 0.16f, 0.28f, 0.34f, 0.66f, 0.52f,
                               0.74f, 0.68f, 0.40f, 0.84f, 0.26f, 0.72f};
        const float cut[] = {0.16f, 0.50f, 0.84f, 0.50f};
        stroke_poly(px, width, height, ox, oy, s, curve, 7, thick, r, g, b);
        stroke_poly(px, width, height, ox, oy, s, cut, 2, thick, r, g, b);
    } else if (tool == 3) {
        const float left[] = {0.40f, 0.20f, 0.18f, 0.50f, 0.40f, 0.80f};
        const float right[] = {0.60f, 0.20f, 0.82f, 0.50f, 0.60f, 0.80f};
        const float slash[] = {0.58f, 0.18f, 0.42f, 0.82f};
        stroke_poly(px, width, height, ox, oy, s, left, 3, thick, r, g, b);
        stroke_poly(px, width, height, ox, oy, s, right, 3, thick, r, g, b);
        stroke_poly(px, width, height, ox, oy, s, slash, 2, thick * 0.8f, r, g, b);
    } else if (tool == 4) {
        stroke_ring(px, width, height, ox + 0.36f * s, oy + 0.58f * s, 0.18f * s, thick, r, g, b);
        stroke_ring(px, width, height, ox + 0.64f * s, oy + 0.40f * s, 0.18f * s, thick, r, g, b);
    } else if (tool == 5) {
        const float left[] = {0.26f, 0.16f, 0.26f, 0.84f};
        const float right[] = {0.74f, 0.16f, 0.74f, 0.84f};
        const float mid[] = {0.26f, 0.50f, 0.74f, 0.50f};
        stroke_poly(px, width, height, ox, oy, s, left, 2, thick, r, g, b);
        stroke_poly(px, width, height, ox, oy, s, right, 2, thick, r, g, b);
        stroke_poly(px, width, height, ox, oy, s, mid, 2, thick, r, g, b);
    } else if (tool == 6) {
        stroke_seg(px, width, height, ox + 0.24f * s, oy + 0.28f * s, ox + 0.24f * s, oy + 0.28f * s, thick * 1.35f, r, g,
                   b);
        stroke_seg(px, width, height, ox + 0.24f * s, oy + 0.50f * s, ox + 0.24f * s, oy + 0.50f * s, thick * 1.35f, r, g,
                   b);
        stroke_seg(px, width, height, ox + 0.24f * s, oy + 0.72f * s, ox + 0.24f * s, oy + 0.72f * s, thick * 1.35f, r, g,
                   b);
        const float a[] = {0.40f, 0.28f, 0.80f, 0.28f};
        const float b2[] = {0.40f, 0.50f, 0.80f, 0.50f};
        const float c[] = {0.40f, 0.72f, 0.68f, 0.72f};
        stroke_poly(px, width, height, ox, oy, s, a, 2, thick, r, g, b);
        stroke_poly(px, width, height, ox, oy, s, b2, 2, thick, r, g, b);
        stroke_poly(px, width, height, ox, oy, s, c, 2, thick, r, g, b);
    } else if (tool == 7) {
        stroke_box(px, width, height, ox, oy, s, 0.18f, 0.18f, 0.82f, 0.82f, thick, r, g, b);
        const float check[] = {0.32f, 0.52f, 0.46f, 0.68f, 0.72f, 0.34f};
        stroke_poly(px, width, height, ox, oy, s, check, 3, thick, r, g, b);
    } else if (tool == 8) {
        const float q0[] = {0.34f, 0.28f, 0.22f, 0.28f, 0.22f, 0.48f, 0.34f, 0.62f};
        const float q1[] = {0.78f, 0.28f, 0.66f, 0.28f, 0.66f, 0.48f, 0.78f, 0.62f};
        stroke_poly(px, width, height, ox, oy, s, q0, 4, thick, r, g, b);
        stroke_poly(px, width, height, ox, oy, s, q1, 4, thick, r, g, b);
    } else if (tool == 9) {
        const float rule[] = {0.14f, 0.50f, 0.86f, 0.50f};
        stroke_poly(px, width, height, ox, oy, s, rule, 2, thick * 1.15f, r, g, b);
    } else if (tool == 10) {
        const float top[] = {0.16f, 0.22f, 0.84f, 0.22f};
        const float mid[] = {0.16f, 0.50f, 0.84f, 0.50f};
        const float bot[] = {0.16f, 0.78f, 0.84f, 0.78f};
        const float left[] = {0.16f, 0.22f, 0.16f, 0.78f};
        const float col[] = {0.50f, 0.22f, 0.50f, 0.78f};
        const float right[] = {0.84f, 0.22f, 0.84f, 0.78f};
        stroke_poly(px, width, height, ox, oy, s, top, 2, thick * 0.85f, r, g, b);
        stroke_poly(px, width, height, ox, oy, s, mid, 2, thick * 0.85f, r, g, b);
        stroke_poly(px, width, height, ox, oy, s, bot, 2, thick * 0.85f, r, g, b);
        stroke_poly(px, width, height, ox, oy, s, left, 2, thick * 0.85f, r, g, b);
        stroke_poly(px, width, height, ox, oy, s, col, 2, thick * 0.85f, r, g, b);
        stroke_poly(px, width, height, ox, oy, s, right, 2, thick * 0.85f, r, g, b);
    } else {
        stroke_box(px, width, height, ox, oy, s, 0.16f, 0.18f, 0.84f, 0.82f, thick, r, g, b);
        const float l0[] = {0.30f, 0.38f, 0.70f, 0.38f};
        const float l1[] = {0.30f, 0.52f, 0.70f, 0.52f};
        const float l2[] = {0.30f, 0.66f, 0.56f, 0.66f};
        stroke_poly(px, width, height, ox, oy, s, l0, 2, thick * 0.8f, r, g, b);
        stroke_poly(px, width, height, ox, oy, s, l1, 2, thick * 0.8f, r, g, b);
        stroke_poly(px, width, height, ox, oy, s, l2, 2, thick * 0.8f, r, g, b);
    }
}
