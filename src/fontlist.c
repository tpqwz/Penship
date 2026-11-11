#include "fontlist.h"

#include <stdio.h>
#include <string.h>

static int name_char(unsigned char c) {
    if (c >= 'a' && c <= 'z') {
        return 1;
    }
    if (c >= 'A' && c <= 'Z') {
        return 1;
    }
    if (c >= '0' && c <= '9') {
        return 1;
    }
    return c == ' ' || c == '-';
}

int fontlist_name_ok(const char *name) {
    static const char *generic[] = {"serif",      "sans-serif", "monospace",    "cursive",       "fantasy",
                                    "system-ui",  "ui-monospace", "ui-sans-serif", "ui-serif",   "ui-rounded",
                                    "emoji",      "math",       "fangsong",     "inherit",       "initial",
                                    "unset"};
    size_t n = 0;
    int g = 0;
    if (name == NULL || name[0] == '\0' || name[0] == ' ' || name[0] == '-') {
        return 0;
    }
    while (name[n] != '\0') {
        if (!name_char((unsigned char)name[n]) || n + 1 >= FONT_NAME_MAX) {
            return 0;
        }
        n++;
    }
    if (name[n - 1] == ' ' || name[n - 1] == '-') {
        return 0;
    }
    for (g = 0; g < (int)(sizeof generic / sizeof generic[0]); g++) {
        if (strcmp(name, generic[g]) == 0) {
            return 0;
        }
    }
    return 1;
}

static size_t skip_ws(const char *s, size_t n, size_t i) {
    while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) {
        i++;
    }
    return i;
}

/* A block comment. When remember is set, `penship-host: token` replaces host. */
static size_t take_comment(const char *s, size_t n, size_t i, char *host, int remember) {
    size_t j = 0;
    size_t body = 0;
    size_t end = 0;
    const char *key = "penship-host:";
    size_t key_n = sizeof "penship-host:" - 1;
    if (i + 1 >= n || s[i] != '/' || s[i + 1] != '*') {
        return i;
    }
    j = i + 2;
    while (j + 1 < n && !(s[j] == '*' && s[j + 1] == '/')) {
        j++;
    }
    end = j + 1 < n ? j + 2 : n;
    if (!remember || host == NULL) {
        return end;
    }
    body = skip_ws(s, j, i + 2);
    if (body + key_n <= j && memcmp(s + body, key, key_n) == 0) {
        size_t h = skip_ws(s, j, body + key_n);
        size_t o = 0;
        host[0] = '\0';
        while (h < j && o + 1 < 24) {
            unsigned char c = (unsigned char)s[h];
            int ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.';
            if (!ok) {
                break;
            }
            host[o++] = (char)c;
            h++;
        }
        host[o] = '\0';
        if (o == 0) {
            host[0] = '\0';
        }
    }
    return end;
}

static int push_name(char names[][FONT_NAME_MAX], int count, int cap, const char *host, const char *family) {
    char shown[FONT_NAME_MAX];
    int wrote = 0;
    int i = 0;
    if (count >= cap || !fontlist_name_ok(family)) {
        return count;
    }
    if (host != NULL && host[0] != '\0') {
        wrote = snprintf(shown, sizeof shown, "%s | %s", host, family);
        if (wrote < 0 || (size_t)wrote >= sizeof shown) {
            wrote = snprintf(shown, sizeof shown, "%s", family);
        }
    } else {
        wrote = snprintf(shown, sizeof shown, "%s", family);
    }
    if (wrote < 0 || (size_t)wrote >= sizeof shown) {
        return count;
    }
    for (i = 0; i < count; i++) {
        if (strcmp(names[i], shown) == 0) {
            return count;
        }
    }
    memcpy(names[count], shown, (size_t)wrote + 1);
    return count + 1;
}

static int take_family(const char *s, size_t start, size_t end, char *out) {
    size_t i = skip_ws(s, end, start);
    size_t o = 0;
    int quoted = 0;
    char quote = 0;
    if (i >= end) {
        return 0;
    }
    if (s[i] == '\'' || s[i] == '"') {
        quoted = 1;
        quote = s[i];
        i++;
    }
    while (i < end && o + 1 < FONT_NAME_MAX) {
        unsigned char c = (unsigned char)s[i];
        if (quoted && c == (unsigned char)quote) {
            break;
        }
        if (!quoted && (c == ';' || c == ',' || c == '}')) {
            break;
        }
        if (!name_char(c)) {
            return 0;
        }
        out[o++] = (char)c;
        i++;
    }
    if (o + 1 >= FONT_NAME_MAX && i < end && name_char((unsigned char)s[i])) {
        return 0;
    }
    while (o > 0 && (out[o - 1] == ' ' || out[o - 1] == '-')) {
        o--;
    }
    out[o] = '\0';
    return o > 0;
}

static int families_in(const char *s, size_t start, size_t end, char names[][FONT_NAME_MAX], int count, int cap,
                       const char *host) {
    size_t i = start;
    const char *key = "font-family";
    while (i + 11 < end && count < cap) {
        size_t next = 0;
        if (s[i] == '/' && i + 1 < end && s[i + 1] == '*') {
            i = take_comment(s, end, i, NULL, 0);
            continue;
        }
        if (memcmp(s + i, key, 11) != 0) {
            i++;
            continue;
        }
        next = skip_ws(s, end, i + 11);
        if (next < end && s[next] == ':') {
            char family[FONT_NAME_MAX];
            if (take_family(s, next + 1, end, family)) {
                count = push_name(names, count, cap, host, family);
            }
            i = next + 1;
            continue;
        }
        i++;
    }
    return count;
}

int fontlist_collect(const char *css, size_t len, char names[][FONT_NAME_MAX], int cap) {
    char host[24];
    size_t i = 0;
    int count = 0;
    const char *rule = "@font-face";
    if (css == NULL || names == NULL || cap < 0) {
        return -1;
    }
    if (cap == 0 || len == 0) {
        return 0;
    }
    host[0] = '\0';
    while (i < len && count < cap) {
        size_t j = 0;
        int depth = 0;
        size_t start = 0;
        if (css[i] == '/' && i + 1 < len && css[i + 1] == '*') {
            i = take_comment(css, len, i, host, 1);
            continue;
        }
        if (i + 10 <= len && memcmp(css + i, rule, 10) == 0) {
            j = skip_ws(css, len, i + 10);
            if (j < len && css[j] == '/') {
                j = take_comment(css, len, j, NULL, 0);
                j = skip_ws(css, len, j);
            }
            if (j >= len || css[j] != '{') {
                i += 10;
                continue;
            }
            depth = 1;
            j++;
            start = j;
            while (j < len && depth > 0) {
                if (css[j] == '/' && j + 1 < len && css[j + 1] == '*') {
                    j = take_comment(css, len, j, NULL, 0);
                    continue;
                }
                if (css[j] == '{') {
                    depth++;
                } else if (css[j] == '}') {
                    depth--;
                }
                if (depth > 0) {
                    j++;
                }
            }
            count = families_in(css, start, j < len ? j : len, names, count, cap, host);
            i = j < len ? j + 1 : len;
            continue;
        }
        i++;
    }
    return count;
}
