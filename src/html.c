#include "html.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct Buf {
    char *s;
    size_t n;
    size_t cap;
};

static int add(struct Buf *b, const char *s, size_t n) {
    if (n > SIZE_MAX - b->n - 1) {
        return -1;
    }
    if (b->n + n + 1 > b->cap) {
        size_t cap = b->cap < 256 ? 256 : b->cap;
        while (cap < b->n + n + 1) {
            if (cap > SIZE_MAX / 2) {
                return -1;
            }
            cap *= 2;
        }
        char *grown = realloc(b->s, cap);
        if (grown == NULL) {
            return -1;
        }
        b->s = grown;
        b->cap = cap;
    }
    if (n > 0) {
        memcpy(b->s + b->n, s, n);
    }
    b->n += n;
    b->s[b->n] = '\0';
    return 0;
}

static int adds(struct Buf *b, const char *s) {
    return add(b, s, strlen(s));
}

static int escape(struct Buf *b, const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        const char *rep = NULL;
        if (c == '&') {
            rep = "&amp;";
        } else if (c == '<') {
            rep = "&lt;";
        } else if (c == '>') {
            rep = "&gt;";
        } else if (c == '"') {
            rep = "&quot;";
        }
        if (rep != NULL) {
            if (adds(b, rep) != 0) {
                return -1;
            }
        } else if (add(b, &c, 1) != 0) {
            return -1;
        }
    }
    return 0;
}

static int fence_mark(const char *s, size_t n) {
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

static int heading_level(const char *s, size_t n, size_t *body) {
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
    while (i < n && (s[i] == ' ' || s[i] == '\t')) {
        i++;
    }
    *body = i;
    return level;
}

static int is_rule(const char *s, size_t n) {
    size_t i = 0;
    while (i < n && s[i] == ' ') {
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

/* A line of = or - under a paragraph. At least one marker. Spaces may separate them. */
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

static int setext_level(const char *s, size_t n) {
    size_t i = 0;
    while (i < n && i < 3 && s[i] == ' ') {
        i++;
    }
    if (i < n && s[i] == '=') {
        return 1;
    }
    return 2;
}

static int list_body(const char *s, size_t n, size_t *body, int *ordered, int *checked) {
    size_t i = 0;
    while (i < n && s[i] == ' ') {
        i++;
    }
    *ordered = 0;
    *checked = -1;
    if (i < n && (s[i] == '-' || s[i] == '*' || s[i] == '+') && i + 1 < n && s[i + 1] == ' ') {
        i += 2;
        if (i + 3 < n && s[i] == '[' && (s[i + 1] == ' ' || s[i + 1] == 'x' || s[i + 1] == 'X') && s[i + 2] == ']' &&
            s[i + 3] == ' ') {
            *checked = s[i + 1] != ' ';
            i += 4;
        }
        *body = i;
        return 1;
    }
    if (i < n && s[i] >= '0' && s[i] <= '9') {
        size_t j = i;
        while (j < n && s[j] >= '0' && s[j] <= '9') {
            j++;
        }
        if (j < n && s[j] == '.' && j + 1 < n && s[j + 1] == ' ') {
            *ordered = 1;
            *body = j + 2;
            return 1;
        }
    }
    return 0;
}

static int quote_body(const char *s, size_t n, size_t *body) {
    size_t i = 0;
    while (i < n && s[i] == ' ') {
        i++;
    }
    if (i < n && s[i] == '>' && (i + 1 == n || s[i + 1] == ' ')) {
        *body = i + 1 < n && s[i + 1] == ' ' ? i + 2 : i + 1;
        return 1;
    }
    return 0;
}

/* Four spaces or a tab, and not a list or quote marker after that indent. */
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

static void indent_body(const char *s, size_t n, const char **body, size_t *bn) {
    if (n > 0 && s[0] == '\t') {
        *body = s + 1;
        *bn = n - 1;
        return;
    }
    if (n >= 4) {
        *body = s + 4;
        *bn = n - 4;
        return;
    }
    *body = s;
    *bn = n;
}

static int has_pipe(const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '|') {
            return 1;
        }
    }
    return 0;
}

static int is_delim_row(const char *s, size_t n) {
    int pipes = 0;
    int marks = 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (c == '|') {
            pipes++;
        } else if (c == '-' || c == ':') {
            marks++;
        } else if (c != ' ' && c != '\t') {
            return 0;
        }
    }
    return pipes > 0 && marks >= 3;
}

enum { REF_MAX = 64, REF_LABEL = 48, REF_URL = 512 };

struct Ref {
    char label[REF_LABEL];
    char url[REF_URL];
    unsigned char n;
    unsigned short url_n;
};

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

/* [label]: destination. The destination is the bare address, with angle brackets removed. */
static int definition_parts(const char *s, size_t n, char *label, size_t *label_n, const char **url, size_t *url_n) {
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
    if (i >= n) {
        return 0;
    }
    if (s[i] == '<') {
        size_t end = i + 1;
        while (end < n && s[end] != '>') {
            end++;
        }
        if (end >= n || end == i + 1) {
            return 0;
        }
        *url = s + i + 1;
        *url_n = end - (i + 1);
    } else {
        size_t end = i;
        while (end < n && s[end] != ' ' && s[end] != '\t') {
            end++;
        }
        if (end == i) {
            return 0;
        }
        *url = s + i;
        *url_n = end - i;
    }
    *label_n = ln;
    return 1;
}

static const struct Ref *ref_lookup(const struct Ref *refs, int nref, const char *s, size_t n) {
    char key[REF_LABEL];
    size_t kn = norm_label(s, n, key, sizeof key);
    if (kn == 0 || refs == NULL) {
        return NULL;
    }
    for (int i = 0; i < nref; i++) {
        if (refs[i].n == kn && memcmp(refs[i].label, key, kn) == 0) {
            return &refs[i];
        }
    }
    return NULL;
}

static int inline_html(struct Buf *b, const struct Ref *refs, int nref, const char *s, size_t n);

static int flank_space(unsigned char c) {
    return c == ' ' || c == '\t';
}

static int flank_left(const char *s, size_t n, size_t i, int len) {
    size_t after = i + (size_t)len;
    int before_ok = i == 0 || flank_space((unsigned char)s[i - 1]) || md_punct((unsigned char)s[i - 1]);
    if (after >= n || flank_space((unsigned char)s[after])) {
        return 0;
    }
    if (md_punct((unsigned char)s[after]) && !before_ok) {
        return 0;
    }
    return 1;
}

static int flank_right(const char *s, size_t n, size_t i, int len) {
    size_t after = i + (size_t)len;
    int after_ok = after >= n || flank_space((unsigned char)s[after]) || md_punct((unsigned char)s[after]);
    if (i == 0 || flank_space((unsigned char)s[i - 1])) {
        return 0;
    }
    if (md_punct((unsigned char)s[i - 1]) && !after_ok) {
        return 0;
    }
    return 1;
}

static int can_open(const char *s, size_t n, size_t i, int len, char marker) {
    int left = flank_left(s, n, i, len);
    int right = flank_right(s, n, i, len);
    if (!left || (marker == '_' && right)) {
        return 0;
    }
    return 1;
}

static int can_close(const char *s, size_t n, size_t i, int len, char marker) {
    int left = flank_left(s, n, i, len);
    int right = flank_right(s, n, i, len);
    if (!right || (marker == '_' && left)) {
        return 0;
    }
    return 1;
}

/* 1 when a same-line * or _ pair was written, 0 when it was not, -1 on failure.
   An underscore inside a word stays a word. */
static int take_emphasis(struct Buf *b, const struct Ref *refs, int nref, const char *s, size_t n, size_t *io) {
    size_t i = *io;
    int len = 0;
    if (i + 1 >= n || (s[i] != '*' && s[i] != '_')) {
        return 0;
    }
    char marker = s[i];
    int strong = s[i + 1] == marker;
    size_t j = 0;
    int found = 0;
    len = strong ? 2 : 1;
    if (!can_open(s, n, i, len, marker)) {
        return 0;
    }
    j = i + (size_t)len;
    while (j < n) {
        size_t skip = escaped_punct(s, n, j);
        if (skip != j) {
            j = skip;
            continue;
        }
        if (strong && j + 1 < n && s[j] == marker && s[j + 1] == marker) {
            if (can_close(s, n, j, 2, marker)) {
                found = 1;
                break;
            }
            j += 2;
            continue;
        }
        if (!strong && s[j] == marker && (j + 1 >= n || s[j + 1] != marker)) {
            if (can_close(s, n, j, 1, marker)) {
                found = 1;
                break;
            }
        }
        j++;
    }
    if (!found) {
        return 0;
    }
    const char *open = strong ? "<strong>" : "<em>";
    const char *close = strong ? "</strong>" : "</em>";
    size_t inner = i + (strong ? (size_t)2 : (size_t)1);
    if (adds(b, open) != 0 || inline_html(b, refs, nref, s + inner, j - inner) != 0 || adds(b, close) != 0) {
        return -1;
    }
    *io = j + (strong ? (size_t)2 : (size_t)1);
    return 1;
}

static int take_autolink(struct Buf *b, const char *s, size_t n, size_t *io) {
    size_t i = *io;
    if (i >= n || s[i] != '<') {
        return 0;
    }
    size_t j = i + 1;
    int mail = 0;
    if (j + 8 <= n && (memcmp(s + j, "https://", 8) == 0 || (j + 7 <= n && memcmp(s + j, "http://", 7) == 0))) {
        while (j < n && s[j] != '>' && s[j] != ' ' && s[j] != '<') {
            j++;
        }
        if (!(j < n && s[j] == '>' && j > i + 8)) {
            return 0;
        }
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
        if (!(at == 1 && dot && j < n && s[j] == '>' && j > i + 3)) {
            return 0;
        }
        mail = 1;
    }
    const char *dest = s + i + 1;
    size_t dn = j - (i + 1);
    if (adds(b, "<a href=\"") != 0 || (mail && adds(b, "mailto:") != 0) || escape(b, dest, dn) != 0 || adds(b, "\">") != 0 ||
        escape(b, dest, dn) != 0 || adds(b, "</a>") != 0) {
        return -1;
    }
    *io = j + 1;
    return 1;
}

static int take_link(struct Buf *b, const struct Ref *refs, int nref, const char *s, size_t n, size_t *io) {
    size_t i = *io;
    if (i >= n || s[i] != '[') {
        return 0;
    }
    size_t rb = i + 1;
    while (rb < n && s[rb] != ']') {
        rb++;
    }
    if (rb >= n || s[rb] != ']') {
        return 0;
    }
    if (rb + 1 < n && s[rb + 1] == '(') {
        size_t url = rb + 2;
        size_t end = url;
        while (end < n && s[end] != ')') {
            end++;
        }
        if (end >= n) {
            return 0;
        }
        if (adds(b, "<a href=\"") != 0 || escape(b, s + url, end - url) != 0 || adds(b, "\">") != 0 ||
            inline_html(b, refs, nref, s + i + 1, rb - (i + 1)) != 0 || adds(b, "</a>") != 0) {
            return -1;
        }
        *io = end + 1;
        return 1;
    }
    const char *key = s + i + 1;
    size_t kn = rb - (i + 1);
    size_t text_n = kn;
    size_t after = rb + 1;
    if (rb + 1 < n && s[rb + 1] == '[') {
        size_t id0 = rb + 2;
        size_t id1 = id0;
        while (id1 < n && s[id1] != ']') {
            id1++;
        }
        if (id1 >= n || s[id1] != ']') {
            return 0;
        }
        if (id1 != id0) {
            key = s + id0;
            kn = id1 - id0;
        }
        after = id1 + 1;
    }
    const struct Ref *ref = ref_lookup(refs, nref, key, kn);
    if (ref == NULL) {
        return 0;
    }
    if (adds(b, "<a href=\"") != 0 || escape(b, ref->url, ref->url_n) != 0 || adds(b, "\">") != 0 ||
        escape(b, s + i + 1, text_n) != 0 || adds(b, "</a>") != 0) {
        return -1;
    }
    *io = after;
    return 1;
}

static int inline_html(struct Buf *b, const struct Ref *refs, int nref, const char *s, size_t n) {
    size_t i = 0;
    while (i < n) {
        if (s[i] == '`') {
            size_t j = i + 1;
            while (j < n && s[j] != '`') {
                j++;
            }
            if (j < n) {
                if (adds(b, "<code>") != 0 || escape(b, s + i + 1, j - i - 1) != 0 || adds(b, "</code>") != 0) {
                    return -1;
                }
                i = j + 1;
                continue;
            }
        }
        size_t esc = escaped_punct(s, n, i);
        if (esc != i) {
            if (escape(b, s + i + 1, 1) != 0) {
                return -1;
            }
            i = esc;
            continue;
        }
        {
            int auto_link = take_autolink(b, s, n, &i);
            if (auto_link < 0) {
                return -1;
            }
            if (auto_link > 0) {
                continue;
            }
        }
        if (i + 1 < n && s[i] == '~' && s[i + 1] == '~') {
            size_t j = i + 2;
            while (j + 1 < n && !(s[j] == '~' && s[j + 1] == '~')) {
                size_t skip = escaped_punct(s, n, j);
                j = skip == j ? j + 1 : skip;
            }
            if (j + 1 < n) {
                if (adds(b, "<del>") != 0 || inline_html(b, refs, nref, s + i + 2, j - (i + 2)) != 0 || adds(b, "</del>") != 0) {
                    return -1;
                }
                i = j + 2;
                continue;
            }
        }
        if (s[i] == '!' && i + 1 < n && s[i + 1] == '[') {
            size_t label = i + 2;
            size_t rb = label;
            while (rb < n && s[rb] != ']') {
                rb++;
            }
            if (rb + 1 < n && s[rb] == ']' && s[rb + 1] == '(') {
                size_t url = rb + 2;
                size_t end = url;
                while (end < n && s[end] != ')') {
                    end++;
                }
                if (end < n) {
                    if (adds(b, "<img alt=\"") != 0 || escape(b, s + label, rb - label) != 0 ||
                        adds(b, "\" src=\"") != 0 || escape(b, s + url, end - url) != 0 || adds(b, "\">") != 0) {
                        return -1;
                    }
                    i = end + 1;
                    continue;
                }
            }
        }
        {
            int link = take_link(b, refs, nref, s, n, &i);
            if (link < 0) {
                return -1;
            }
            if (link > 0) {
                continue;
            }
        }
        {
            int em = take_emphasis(b, refs, nref, s, n, &i);
            if (em < 0) {
                return -1;
            }
            if (em > 0) {
                continue;
            }
        }
        if (escape(b, s + i, 1) != 0) {
            return -1;
        }
        i++;
    }
    return 0;
}

static int cells(struct Buf *b, const struct Ref *refs, int nref, const char *s, size_t n, const char *tag) {
    size_t i = 0;
    if (i < n && s[i] == '|') {
        i++;
    }
    while (i < n) {
        size_t start = i;
        while (i < n && s[i] != '|') {
            i++;
        }
        size_t end = i;
        while (start < end && (s[start] == ' ' || s[start] == '\t')) {
            start++;
        }
        while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t')) {
            end--;
        }
        if (adds(b, "<") != 0 || adds(b, tag) != 0 || adds(b, ">") != 0 ||
            inline_html(b, refs, nref, s + start, end - start) != 0 ||
            adds(b, "</") != 0 || adds(b, tag) != 0 || adds(b, ">") != 0) {
            return -1;
        }
        if (i < n && s[i] == '|') {
            i++;
        }
    }
    return 0;
}

struct Line {
    const char *s;
    size_t n;
};

static int next_line(const char *text, size_t n, size_t *i, struct Line *line) {
    if (*i > n) {
        return 0;
    }
    size_t start = *i;
    while (*i < n && text[*i] != '\n') {
        (*i)++;
    }
    line->s = text + start;
    line->n = *i - start;
    if (*i < n && text[*i] == '\n') {
        (*i)++;
    }
    return 1;
}

static int plain_line(const char *s, size_t n) {
    size_t body = 0;
    if (n == 0 || fence_mark(s, n) != 0) {
        return 0;
    }
    if (heading_level(s, n, &body) > 0 || is_rule(s, n)) {
        return 0;
    }
    return 1;
}

static int peek_setext(const char *text, size_t n, size_t *i, int *level) {
    size_t save = *i;
    struct Line under;
    if (!next_line(text, n, i, &under) || !is_setext(under.s, under.n)) {
        *i = save;
        return 0;
    }
    *level = setext_level(under.s, under.n);
    return 1;
}

static int emit_heading(struct Buf *b, const struct Ref *refs, int nref, int level, const char *s, size_t n) {
    char open[8];
    char close[8];
    if (level < 1) {
        level = 1;
    }
    if (level > 6) {
        level = 6;
    }
    open[0] = '<';
    open[1] = 'h';
    open[2] = (char)('0' + level);
    open[3] = '>';
    open[4] = '\0';
    close[0] = '<';
    close[1] = '/';
    close[2] = 'h';
    close[3] = (char)('0' + level);
    close[4] = '>';
    close[5] = '\n';
    close[6] = '\0';
    if (adds(b, open) != 0 || inline_html(b, refs, nref, s, n) != 0 || adds(b, close) != 0) {
        return -1;
    }
    return 0;
}

/* The label stays on the page as a hidden mark. The copy keeps the address. */
static int emit_definition(struct Buf *b, const char *s, size_t n) {
    char label[REF_LABEL];
    size_t ln = 0;
    const char *url = NULL;
    size_t un = 0;
    if (!definition_parts(s, n, label, &ln, &url, &un)) {
        return 0;
    }
    if (adds(b, "<p><a href=\"") != 0 || escape(b, url, un) != 0 || adds(b, "\">") != 0 || escape(b, url, un) != 0 ||
        adds(b, "</a></p>\n") != 0) {
        return -1;
    }
    return 1;
}

/* shortcut: a reference destination of 512 bytes or more is not linked from the label. The definition line still shows it. */
static void gather_refs(const char *text, size_t n, struct Ref *refs, int *nref) {
    int fence = 0;
    size_t i = 0;
    *nref = 0;
    while (i <= n && *nref < REF_MAX) {
        struct Line line;
        int mark;
        char label[REF_LABEL];
        size_t ln = 0;
        const char *url = NULL;
        size_t un = 0;
        int seen = 0;
        if (!next_line(text, n, &i, &line)) {
            break;
        }
        if (line.n == 0 && i >= n) {
            break;
        }
        mark = fence_mark(line.s, line.n);
        if (mark != 0) {
            fence = fence == mark ? 0 : (fence == 0 ? mark : fence);
            continue;
        }
        if (fence != 0 || line.n == 0) {
            continue;
        }
        if (!definition_parts(line.s, line.n, label, &ln, &url, &un) || ln == 0 || ln >= REF_LABEL || un == 0 ||
            un >= REF_URL) {
            continue;
        }
        for (int r = 0; r < *nref; r++) {
            if (refs[r].n == ln && memcmp(refs[r].label, label, ln) == 0) {
                seen = 1;
                break;
            }
        }
        if (seen) {
            continue;
        }
        memcpy(refs[*nref].label, label, ln);
        refs[*nref].label[ln] = '\0';
        refs[*nref].n = (unsigned char)ln;
        memcpy(refs[*nref].url, url, un);
        refs[*nref].url[un] = '\0';
        refs[*nref].url_n = (unsigned short)un;
        (*nref)++;
    }
}

int html_export(const char *text, size_t n, char **out, size_t *out_len) {
    if (out == NULL) {
        return -1;
    }
    *out = NULL;
    if (out_len != NULL) {
        *out_len = 0;
    }
    if (text == NULL) {
        text = "";
        n = 0;
    }
    struct Buf b = {0};
    struct Ref refs[REF_MAX];
    int nref = 0;
    if (adds(&b, "<!DOCTYPE html>\n<html>\n<head><meta charset=\"utf-8\"></head>\n<body>\n") != 0) {
        free(b.s);
        return -1;
    }
    gather_refs(text, n, refs, &nref);
    size_t i = 0;
    while (i <= n) {
        struct Line line;
        if (!next_line(text, n, &i, &line)) {
            break;
        }
        if (line.n == 0) {
            if (i >= n) {
                break;
            }
            continue;
        }
        int mark = fence_mark(line.s, line.n);
        if (mark != 0) {
            if (adds(&b, "<pre><code>") != 0) {
                free(b.s);
                return -1;
            }
            while (i <= n) {
                struct Line inner;
                if (!next_line(text, n, &i, &inner) || (inner.n == 0 && i >= n)) {
                    break;
                }
                if (fence_mark(inner.s, inner.n) == mark) {
                    break;
                }
                if (escape(&b, inner.s, inner.n) != 0 || adds(&b, "\n") != 0) {
                    free(b.s);
                    return -1;
                }
            }
            if (adds(&b, "</code></pre>\n") != 0) {
                free(b.s);
                return -1;
            }
            continue;
        }
        size_t body = 0;
        int level = heading_level(line.s, line.n, &body);
        if (level > 0) {
            if (emit_heading(&b, refs, nref, level, line.s + body, line.n - body) != 0) {
                free(b.s);
                return -1;
            }
            continue;
        }
        if (plain_line(line.s, line.n)) {
            int setext = 0;
            if (peek_setext(text, n, &i, &setext)) {
                if (emit_heading(&b, refs, nref, setext, line.s, line.n) != 0) {
                    free(b.s);
                    return -1;
                }
                continue;
            }
        }
        if (is_rule(line.s, line.n)) {
            if (adds(&b, "<hr>\n") != 0) {
                free(b.s);
                return -1;
            }
            continue;
        }
        int ordered = 0;
        int checked = -1;
        if (list_body(line.s, line.n, &body, &ordered, &checked)) {
            if (adds(&b, ordered ? "<ol>\n" : "<ul>\n") != 0) {
                free(b.s);
                return -1;
            }
            for (;;) {
                if (adds(&b, "<li>") != 0) {
                    free(b.s);
                    return -1;
                }
                if (checked >= 0) {
                    if (adds(&b, checked ? "<input type=\"checkbox\" disabled checked> " : "<input type=\"checkbox\" disabled> ") !=
                        0) {
                        free(b.s);
                        return -1;
                    }
                }
                if (inline_html(&b, refs, nref, line.s + body, line.n - body) != 0 || adds(&b, "</li>\n") != 0) {
                    free(b.s);
                    return -1;
                }
                size_t save = i;
                struct Line next;
                if (!next_line(text, n, &i, &next)) {
                    break;
                }
                int next_ordered = 0;
                int next_checked = -1;
                size_t next_body = 0;
                if (!list_body(next.s, next.n, &next_body, &next_ordered, &next_checked) || next_ordered != ordered) {
                    i = save;
                    break;
                }
                line = next;
                body = next_body;
                checked = next_checked;
            }
            if (adds(&b, ordered ? "</ol>\n" : "</ul>\n") != 0) {
                free(b.s);
                return -1;
            }
            continue;
        }
        if (quote_body(line.s, line.n, &body)) {
            if (adds(&b, "<blockquote>\n") != 0) {
                free(b.s);
                return -1;
            }
            for (;;) {
                if (inline_html(&b, refs, nref, line.s + body, line.n - body) != 0 || adds(&b, "\n") != 0) {
                    free(b.s);
                    return -1;
                }
                size_t save = i;
                struct Line next;
                if (!next_line(text, n, &i, &next)) {
                    break;
                }
                size_t next_body = 0;
                if (!quote_body(next.s, next.n, &next_body)) {
                    i = save;
                    break;
                }
                line = next;
                body = next_body;
            }
            if (adds(&b, "</blockquote>\n") != 0) {
                free(b.s);
                return -1;
            }
            continue;
        }
        size_t look = i;
        struct Line peek;
        int table = 0;
        if (has_pipe(line.s, line.n) && next_line(text, n, &look, &peek) && is_delim_row(peek.s, peek.n)) {
            table = 1;
        }
        if (table) {
            i = look;
            if (adds(&b, "<table>\n<tr>") != 0 || cells(&b, refs, nref, line.s, line.n, "th") != 0 || adds(&b, "</tr>\n") != 0) {
                free(b.s);
                return -1;
            }
            while (i <= n) {
                size_t save = i;
                struct Line row;
                if (!next_line(text, n, &i, &row) || row.n == 0 || !has_pipe(row.s, row.n)) {
                    i = save;
                    break;
                }
                if (adds(&b, "<tr>") != 0 || cells(&b, refs, nref, row.s, row.n, "td") != 0 || adds(&b, "</tr>\n") != 0) {
                    free(b.s);
                    return -1;
                }
            }
            if (adds(&b, "</table>\n") != 0) {
                free(b.s);
                return -1;
            }
            continue;
        }
        if (is_indented_code(line.s, line.n)) {
            if (adds(&b, "<pre><code>") != 0) {
                free(b.s);
                return -1;
            }
            for (;;) {
                const char *code = NULL;
                size_t cn = 0;
                indent_body(line.s, line.n, &code, &cn);
                if (escape(&b, code, cn) != 0 || adds(&b, "\n") != 0) {
                    free(b.s);
                    return -1;
                }
                size_t save = i;
                struct Line next;
                if (!next_line(text, n, &i, &next)) {
                    break;
                }
                if (is_indented_code(next.s, next.n)) {
                    line = next;
                    continue;
                }
                if (next.n == 0) {
                    size_t save_blank = i;
                    struct Line after;
                    if (next_line(text, n, &i, &after) && is_indented_code(after.s, after.n)) {
                        if (adds(&b, "\n") != 0) {
                            free(b.s);
                            return -1;
                        }
                        line = after;
                        continue;
                    }
                    i = save_blank;
                    break;
                }
                i = save;
                break;
            }
            if (adds(&b, "</code></pre>\n") != 0) {
                free(b.s);
                return -1;
            }
            continue;
        }
        {
            int defined = emit_definition(&b, line.s, line.n);
            if (defined < 0) {
                free(b.s);
                return -1;
            }
            if (defined > 0) {
                continue;
            }
        }
        if (adds(&b, "<p>") != 0 || inline_html(&b, refs, nref, line.s, line.n) != 0 || adds(&b, "</p>\n") != 0) {
            free(b.s);
            return -1;
        }
        if (i >= n) {
            break;
        }
    }
    if (adds(&b, "</body>\n</html>\n") != 0) {
        free(b.s);
        return -1;
    }
    *out = b.s;
    if (out_len != NULL) {
        *out_len = b.n;
    }
    return 0;
}
