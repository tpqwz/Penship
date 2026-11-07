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

static int inline_html(struct Buf *b, const char *s, size_t n) {
    size_t i = 0;
    while (i < n) {
        if (s[i] == '`' ) {
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
        if (i + 1 < n && s[i] == '*' && s[i + 1] == '*') {
            size_t j = i + 2;
            while (j + 1 < n && !(s[j] == '*' && s[j + 1] == '*')) {
                j++;
            }
            if (j + 1 < n) {
                if (adds(b, "<strong>") != 0 || inline_html(b, s + i + 2, j - i - 2) != 0 || adds(b, "</strong>") != 0) {
                    return -1;
                }
                i = j + 2;
                continue;
            }
        }
        if (s[i] == '*' && (i + 1 >= n || s[i + 1] != '*')) {
            size_t j = i + 1;
            while (j < n && s[j] != '*') {
                j++;
            }
            if (j < n && j > i + 1) {
                if (adds(b, "<em>") != 0 || inline_html(b, s + i + 1, j - i - 1) != 0 || adds(b, "</em>") != 0) {
                    return -1;
                }
                i = j + 1;
                continue;
            }
        }
        if (i + 1 < n && s[i] == '~' && s[i + 1] == '~') {
            size_t j = i + 2;
            while (j + 1 < n && !(s[j] == '~' && s[j + 1] == '~')) {
                j++;
            }
            if (j + 1 < n) {
                if (adds(b, "<del>") != 0 || inline_html(b, s + i + 2, j - i - 2) != 0 || adds(b, "</del>") != 0) {
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
                    if (escape(b, s + label, rb - label) != 0) {
                        return -1;
                    }
                    i = end + 1;
                    continue;
                }
            }
        }
        if (s[i] == '[') {
            size_t label = i + 1;
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
                    if (adds(b, "<a href=\"") != 0 || escape(b, s + url, end - url) != 0 || adds(b, "\">") != 0 ||
                        inline_html(b, s + label, rb - label) != 0 || adds(b, "</a>") != 0) {
                        return -1;
                    }
                    i = end + 1;
                    continue;
                }
            }
        }
        if (escape(b, s + i, 1) != 0) {
            return -1;
        }
        i++;
    }
    return 0;
}

static int cells(struct Buf *b, const char *s, size_t n, const char *tag) {
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
        if (adds(b, "<") != 0 || adds(b, tag) != 0 || adds(b, ">") != 0 || inline_html(b, s + start, end - start) != 0 ||
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
    if (adds(&b, "<!DOCTYPE html>\n<html>\n<head><meta charset=\"utf-8\"></head>\n<body>\n") != 0) {
        free(b.s);
        return -1;
    }
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
                if (!next_line(text, n, &i, &inner)) {
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
            char open[8];
            char close[8];
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
            if (adds(&b, open) != 0 || inline_html(&b, line.s + body, line.n - body) != 0 || adds(&b, close) != 0) {
                free(b.s);
                return -1;
            }
            continue;
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
                if (inline_html(&b, line.s + body, line.n - body) != 0 || adds(&b, "</li>\n") != 0) {
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
                if (inline_html(&b, line.s + body, line.n - body) != 0 || adds(&b, "\n") != 0) {
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
            if (adds(&b, "<table>\n<tr>") != 0 || cells(&b, line.s, line.n, "th") != 0 || adds(&b, "</tr>\n") != 0) {
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
                if (adds(&b, "<tr>") != 0 || cells(&b, row.s, row.n, "td") != 0 || adds(&b, "</tr>\n") != 0) {
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
        if (adds(&b, "<p>") != 0 || inline_html(&b, line.s, line.n) != 0 || adds(&b, "</p>\n") != 0) {
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
