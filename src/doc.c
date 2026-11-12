#define _POSIX_C_SOURCE 200809L

#include "doc.h"

#include "utf8.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DOC_UNDO_MAX 32
#define DOC_UNDO_BYTES (1u << 16)

struct Edit {
    size_t at;
    size_t n_new;
    size_t n_old;
    char *old;
    size_t caret;
    size_t anchor;
};

/* A leaf holds at most this many bytes. A larger edit splits the leaf. */
#define ROPE_LEAF 4096

struct Piece {
    char *data;
    size_t len;
    size_t cap;
};

struct Doc {
    struct Piece *piece;
    size_t npiece;
    size_t piece_cap;
    char *bytes;
    size_t len;
    size_t cap;
    size_t caret;
    size_t anchor;
    struct Edit undo[DOC_UNDO_MAX];
    size_t undo_n;
    size_t undo_bytes;
    /* SIZE_MAX means the bytes match the last save of saved_path. */
    size_t dirty_at;
    size_t saved_len;
    char saved_path[1024];
};

static void piece_clear(Doc *doc) {
    for (size_t i = 0; i < doc->npiece; i++) {
        free(doc->piece[i].data);
    }
    free(doc->piece);
    doc->piece = NULL;
    doc->npiece = 0;
    doc->piece_cap = 0;
}

static int piece_slot(Doc *doc, size_t at) {
    if (doc->npiece > SIZE_MAX - 1) {
        return -1;
    }
    if (doc->npiece + 1 > doc->piece_cap) {
        size_t cap = doc->piece_cap == 0 ? 4 : doc->piece_cap * 2;
        struct Piece *grown = realloc(doc->piece, cap * sizeof *grown);
        if (grown == NULL) {
            return -1;
        }
        doc->piece = grown;
        doc->piece_cap = cap;
    }
    if (at > doc->npiece) {
        at = doc->npiece;
    }
    memmove(doc->piece + at + 1, doc->piece + at, (doc->npiece - at) * sizeof *doc->piece);
    memset(&doc->piece[at], 0, sizeof doc->piece[at]);
    doc->npiece++;
    return 0;
}

static int piece_alloc(struct Piece *piece, size_t cap) {
    if (cap < 1) {
        cap = 1;
    }
    char *data = malloc(cap);
    if (data == NULL) {
        return -1;
    }
    piece->data = data;
    piece->cap = cap;
    piece->len = 0;
    return 0;
}

/* Find the leaf that contains byte at. at == len lands on the end of the last leaf. */
static int locate(const Doc *doc, size_t at, size_t *pi, size_t *inner) {
    if (doc->npiece == 0 || at > doc->len) {
        return -1;
    }
    size_t acc = 0;
    for (size_t i = 0; i < doc->npiece; i++) {
        size_t next = acc + doc->piece[i].len;
        if (at < next || i + 1 == doc->npiece) {
            *pi = i;
            *inner = at - acc;
            return 0;
        }
        acc = next;
    }
    return -1;
}

Doc *doc_new(void) {
    Doc *doc = calloc(1, sizeof *doc);
    if (doc == NULL) {
        return NULL;
    }
    if (piece_slot(doc, 0) != 0 || piece_alloc(&doc->piece[0], 1) != 0) {
        piece_clear(doc);
        free(doc);
        return NULL;
    }
    doc->bytes = calloc(1, 1);
    if (doc->bytes == NULL) {
        piece_clear(doc);
        free(doc);
        return NULL;
    }
    doc->cap = 1;
    doc->dirty_at = 0;
    doc->saved_len = SIZE_MAX;
    return doc;
}

static void undo_clear(Doc *doc) {
    for (size_t i = 0; i < doc->undo_n; i++) {
        free(doc->undo[i].old);
        doc->undo[i].old = NULL;
    }
    doc->undo_n = 0;
    doc->undo_bytes = 0;
}

void doc_free(Doc *doc) {
    if (doc == NULL) {
        return;
    }
    undo_clear(doc);
    piece_clear(doc);
    free(doc->bytes);
    free(doc);
}

const char *doc_text(const Doc *doc, size_t *len) {
    if (len != NULL) {
        *len = doc->len;
    }
    return doc->bytes;
}

size_t doc_caret(const Doc *doc) {
    return doc->caret;
}

size_t doc_anchor(const Doc *doc) {
    return doc->anchor;
}

static void sel_bounds(const Doc *doc, size_t *lo, size_t *hi) {
    if (doc->anchor < doc->caret) {
        *lo = doc->anchor;
        *hi = doc->caret;
    } else {
        *lo = doc->caret;
        *hi = doc->anchor;
    }
}

static int piece_room(struct Piece *piece, size_t need) {
    if (need <= piece->cap) {
        return 0;
    }
    size_t cap = piece->cap < 64 ? 64 : piece->cap;
    while (cap < need) {
        if (cap > SIZE_MAX / 2) {
            return -1;
        }
        cap *= 2;
    }
    if (cap > ROPE_LEAF) {
        cap = need > ROPE_LEAF ? need : ROPE_LEAF;
    }
    char *data = realloc(piece->data, cap);
    if (data == NULL) {
        return -1;
    }
    piece->data = data;
    piece->cap = cap;
    return 0;
}

static void undo_drop_oldest(Doc *doc) {
    if (doc->undo_n == 0) {
        return;
    }
    if (doc->undo_bytes >= doc->undo[0].n_old) {
        doc->undo_bytes -= doc->undo[0].n_old;
    } else {
        doc->undo_bytes = 0;
    }
    free(doc->undo[0].old);
    memmove(doc->undo, doc->undo + 1, (doc->undo_n - 1) * sizeof doc->undo[0]);
    doc->undo_n--;
}

/* Drop cut bytes at at from the leaves. The flat mirror is updated by the caller. */
static int rope_cut(Doc *doc, size_t at, size_t cut) {
    while (cut > 0) {
        size_t pi = 0;
        size_t inner = 0;
        if (locate(doc, at, &pi, &inner) != 0) {
            return -1;
        }
        struct Piece *piece = &doc->piece[pi];
        if (piece->len == 0) {
            if (doc->npiece == 1) {
                return cut == 0 ? 0 : -1;
            }
            free(piece->data);
            memmove(doc->piece + pi, doc->piece + pi + 1, (doc->npiece - pi - 1) * sizeof *doc->piece);
            doc->npiece--;
            continue;
        }
        size_t avail = piece->len - inner;
        size_t take = cut < avail ? cut : avail;
        memmove(piece->data + inner, piece->data + inner + take, piece->len - inner - take);
        piece->len -= take;
        if (doc->len < take) {
            return -1;
        }
        doc->len -= take;
        cut -= take;
        if (piece->len == 0 && doc->npiece > 1) {
            free(piece->data);
            memmove(doc->piece + pi, doc->piece + pi + 1, (doc->npiece - pi - 1) * sizeof *doc->piece);
            doc->npiece--;
        }
    }
    return 0;
}

/* Insert n bytes at at. n is at most one leaf. */
static int rope_put(Doc *doc, size_t at, const char *in, size_t n) {
    if (n == 0) {
        return 0;
    }
    size_t pi = 0;
    size_t inner = 0;
    if (locate(doc, at, &pi, &inner) != 0) {
        return -1;
    }
    struct Piece *piece = &doc->piece[pi];
    if (piece->len + n <= ROPE_LEAF && piece_room(piece, piece->len + n) == 0) {
        memmove(piece->data + inner + n, piece->data + inner, piece->len - inner);
        memcpy(piece->data + inner, in, n);
        piece->len += n;
        doc->len += n;
        return 0;
    }
    /* Split this leaf and put the new bytes in their own leaf. */
    size_t right_n = piece->len - inner;
    char *right = NULL;
    if (right_n > 0) {
        right = malloc(right_n);
        if (right == NULL) {
            return -1;
        }
        memcpy(right, piece->data + inner, right_n);
    }
    piece->len = inner;
    if (piece_slot(doc, pi + 1) != 0) {
        free(right);
        return -1;
    }
    if (piece_alloc(&doc->piece[pi + 1], n) != 0) {
        free(right);
        return -1;
    }
    memcpy(doc->piece[pi + 1].data, in, n);
    doc->piece[pi + 1].len = n;
    doc->len += n;
    if (right_n > 0) {
        if (piece_slot(doc, pi + 2) != 0 || piece_alloc(&doc->piece[pi + 2], right_n) != 0) {
            free(right);
            return -1;
        }
        memcpy(doc->piece[pi + 2].data, right, right_n);
        doc->piece[pi + 2].len = right_n;
        free(right);
    }
    return 0;
}

static int splice(Doc *doc, size_t at, size_t cut, const char *in, size_t n) {
    if (at > doc->len || cut > doc->len - at || n > SIZE_MAX - (doc->len - cut) - 1) {
        return -1;
    }
    size_t old_len = doc->len;
    size_t new_len = doc->len - cut + n;
    if (rope_cut(doc, at, cut) != 0) {
        return -1;
    }
    size_t left = n;
    size_t off = 0;
    while (left > 0) {
        size_t chunk = left > ROPE_LEAF ? ROPE_LEAF : left;
        if (rope_put(doc, at + off, in + off, chunk) != 0) {
            return -1;
        }
        off += chunk;
        left -= chunk;
    }
    doc->len = new_len;
    /* The leaves already hold the edit. Move only the flat tail the painter reads. */
    if (doc->cap < new_len + 1) {
        size_t cap = doc->cap < 64 ? 64 : doc->cap;
        while (cap < new_len + 1) {
            if (cap > SIZE_MAX / 2) {
                return -1;
            }
            cap *= 2;
        }
        char *grown = realloc(doc->bytes, cap);
        if (grown == NULL) {
            return -1;
        }
        doc->bytes = grown;
        doc->cap = cap;
    }
    {
        size_t tail = old_len - (at + cut);
        if (tail > 0 && n != cut) {
            memmove(doc->bytes + at + n, doc->bytes + at + cut, tail);
        }
        if (n > 0 && in != NULL) {
            memcpy(doc->bytes + at, in, n);
        }
        doc->bytes[new_len] = '\0';
    }
    if (doc->dirty_at > at) {
        doc->dirty_at = at;
    }
    return 0;
}

/* Replace [at, at+cut) with in. A deleted span bigger than the undo budget is not recorded. */
static int edit(Doc *doc, size_t at, size_t cut, const char *in, size_t n) {
    if (n > 0 && (in == NULL || !utf8_valid(in, n))) {
        return -1;
    }
    char *copy = NULL;
    int record = cut <= DOC_UNDO_BYTES;
    if (record && cut > 0) {
        copy = malloc(cut);
        if (copy == NULL) {
            record = 0;
        } else {
            memcpy(copy, doc->bytes + at, cut);
        }
    }
    size_t prev_caret = doc->caret;
    size_t prev_anchor = doc->anchor;
    if (splice(doc, at, cut, in, n) != 0) {
        free(copy);
        return -1;
    }
    doc->caret = at + n;
    doc->anchor = doc->caret;
    if (record) {
        while (doc->undo_n >= DOC_UNDO_MAX || (doc->undo_n > 0 && cut > DOC_UNDO_BYTES - doc->undo_bytes)) {
            undo_drop_oldest(doc);
        }
        if (doc->undo_n < DOC_UNDO_MAX && cut <= DOC_UNDO_BYTES - doc->undo_bytes) {
            struct Edit *e = &doc->undo[doc->undo_n++];
            e->at = at;
            e->n_new = n;
            e->n_old = cut;
            e->old = copy;
            e->caret = prev_caret;
            e->anchor = prev_anchor;
            doc->undo_bytes += cut;
            copy = NULL;
        }
    }
    free(copy);
    return 0;
}

/* shortcut: a marker longer than 64 bytes stays a plain newline. Ordered numbers are
   decimal, at most 9 digits, and the next line is one higher. */
enum { MARK_MAX = 64 };

static int append_mark(char *out, size_t cap, size_t *n, const char *src, size_t sn) {
    if (src == NULL || sn > cap || *n > cap - sn) {
        return -1;
    }
    if (sn > 0) {
        memcpy(out + *n, src, sn);
    }
    *n += sn;
    return 0;
}

static int format_number(char *dst, size_t cap, uint32_t value) {
    char tmp[10];
    int n = 0;
    uint32_t v = value;
    do {
        tmp[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    } while (v > 0 && n < 10);
    if ((size_t)n + 2 > cap) {
        return -1;
    }
    for (int i = 0; i < n; i++) {
        dst[i] = tmp[n - 1 - i];
    }
    dst[n] = '.';
    dst[n + 1] = ' ';
    return n + 2;
}

/* Length of the marker for the next line. 0 means this line has no list or quote.
   -1 means the marker does not fit. *old_len is the marker on this line. */
static int next_marker(const char *s, size_t n, char *out, size_t cap, size_t *old_len) {
    size_t i = 0;
    while (i < n && i < 24 && s[i] == ' ') {
        i++;
    }
    size_t quote = i;
    while (quote < n && s[quote] == '>') {
        quote++;
    }
    int has_quote = quote > i;
    if (has_quote && quote < n && s[quote] == ' ') {
        quote++;
    }
    size_t list = quote;
    int kind = 0;
    char bullet = 0;
    uint32_t num = 0;
    size_t end = list;
    if (list < n && (s[list] == '-' || s[list] == '+' || s[list] == '*') && list + 1 < n && s[list + 1] == ' ') {
        bullet = s[list];
        kind = 1;
        end = list + 2;
        if (end + 3 < n && s[end] == '[' && (s[end + 1] == ' ' || s[end + 1] == 'x' || s[end + 1] == 'X') &&
            s[end + 2] == ']' && s[end + 3] == ' ') {
            kind = 2;
            end += 4;
        }
    } else {
        size_t d = list;
        int digits = 0;
        while (d < n && digits < 9 && s[d] >= '0' && s[d] <= '9') {
            uint32_t dig = (uint32_t)(s[d] - '0');
            if (num > (UINT32_MAX - dig) / 10u) {
                return 0;
            }
            num = num * 10u + dig;
            digits++;
            d++;
        }
        if (digits > 0 && d + 1 < n && s[d] == '.' && s[d + 1] == ' ') {
            kind = 3;
            end = d + 2;
        }
    }
    if (!has_quote && kind == 0) {
        return 0;
    }
    if (end > MARK_MAX) {
        return -1;
    }
    *old_len = end;
    size_t w = 0;
    if (append_mark(out, cap, &w, s, quote) != 0) {
        return -1;
    }
    if (kind == 1) {
        char m[2];
        m[0] = bullet;
        m[1] = ' ';
        if (append_mark(out, cap, &w, m, 2) != 0) {
            return -1;
        }
    } else if (kind == 2) {
        char m[6];
        m[0] = bullet;
        m[1] = ' ';
        m[2] = '[';
        m[3] = ' ';
        m[4] = ']';
        m[5] = ' ';
        if (append_mark(out, cap, &w, m, 6) != 0) {
            return -1;
        }
    } else if (kind == 3) {
        uint32_t next = num < 999999999u ? num + 1u : num;
        char tmp[12];
        int tn = format_number(tmp, sizeof tmp, next);
        if (tn < 0 || append_mark(out, cap, &w, tmp, (size_t)tn) != 0) {
            return -1;
        }
    }
    return (int)w;
}

static int table_down(Doc *doc);

int doc_insert(Doc *doc, const char *utf8, size_t n) {
    if (n == 0) {
        return 0;
    }
    size_t lo = 0;
    size_t hi = 0;
    sel_bounds(doc, &lo, &hi);
    if (n == 1 && utf8 != NULL && utf8[0] == '\n' && lo == hi) {
        if (table_down(doc) == 0) {
            return 0;
        }
        size_t start = doc->caret;
        while (start > 0 && doc->bytes[start - 1] != '\n') {
            start--;
        }
        size_t line_end = doc->caret;
        while (line_end < doc->len && doc->bytes[line_end] != '\n') {
            line_end++;
        }
        char marker[MARK_MAX];
        size_t old_len = 0;
        int nw = next_marker(doc->bytes + start, line_end - start, marker, sizeof marker, &old_len);
        if (nw > 0 && doc->caret >= start && doc->caret - start >= old_len && (size_t)nw <= sizeof marker) {
            if (doc->caret == line_end && doc->caret - start == old_len) {
                return edit(doc, start, old_len, NULL, 0);
            }
            char buf[1 + MARK_MAX];
            buf[0] = '\n';
            memcpy(buf + 1, marker, (size_t)nw);
            return edit(doc, doc->caret, 0, buf, (size_t)nw + 1);
        }
    }
    return edit(doc, lo, hi - lo, utf8, n);
}

/* 1 when s[at, at+n) is mark and the same mark character does not continue past it. */
static int mark_at(const char *s, size_t len, size_t at, const char *mark, size_t n) {
    if (n == 0 || at > len || n > len - at) {
        return 0;
    }
    if (memcmp(s + at, mark, n) != 0) {
        return 0;
    }
    unsigned char edge = (unsigned char)mark[0];
    int uniform = 1;
    for (size_t i = 1; i < n; i++) {
        if ((unsigned char)mark[i] != edge) {
            uniform = 0;
        }
    }
    if (!uniform) {
        return 1;
    }
    if (at > 0 && (unsigned char)s[at - 1] == edge) {
        return 0;
    }
    if (at + n < len && (unsigned char)s[at + n] == edge) {
        return 0;
    }
    return 1;
}

static int replace_span(Doc *doc, size_t at, size_t cut, const char *in, size_t n) {
    char *copy = NULL;
    if (n > 0) {
        copy = malloc(n);
        if (copy == NULL) {
            return -1;
        }
        memcpy(copy, in, n);
    }
    int rc = edit(doc, at, cut, copy, n);
    free(copy);
    return rc;
}

int doc_mark(Doc *doc, const char *mark, size_t n) {
    if (doc == NULL || doc->bytes == NULL || mark == NULL || n == 0 || n > 8) {
        return -1;
    }
    if (!utf8_valid(mark, n)) {
        return -1;
    }
    for (size_t i = 0; i < n; i++) {
        if (mark[i] == '\n') {
            return -1;
        }
    }
    size_t lo = 0;
    size_t hi = 0;
    sel_bounds(doc, &lo, &hi);
    size_t len = doc->len;
    if (hi < lo || hi > len) {
        return -1;
    }
    if (hi > lo && hi - lo >= n * 2 && mark_at(doc->bytes, len, lo, mark, n) &&
        mark_at(doc->bytes, len, hi - n, mark, n)) {
        return replace_span(doc, lo, hi - lo, doc->bytes + lo + n, hi - lo - n * 2);
    }
    if (n <= len && lo >= n && hi <= len - n && mark_at(doc->bytes, len, lo - n, mark, n) &&
        mark_at(doc->bytes, len, hi, mark, n)) {
        return replace_span(doc, lo - n, hi - lo + n * 2, doc->bytes + lo, hi - lo);
    }
    if (n > SIZE_MAX / 2 || hi - lo > SIZE_MAX - n * 2) {
        return -1;
    }
    size_t sel = hi - lo;
    size_t out_n = sel + n * 2;
    char *buf = malloc(out_n > 0 ? out_n : 1);
    if (buf == NULL) {
        return -1;
    }
    memcpy(buf, mark, n);
    if (sel > 0) {
        memcpy(buf + n, doc->bytes + lo, sel);
    }
    memcpy(buf + n + sel, mark, n);
    int rc = edit(doc, lo, sel, buf, out_n);
    free(buf);
    if (rc == 0 && sel == 0) {
        doc->caret = lo + n;
        doc->anchor = doc->caret;
    }
    return rc;
}

static int buf_add(char *out, size_t cap, size_t *n, const char *src, size_t sn) {
    if (out == NULL || n == NULL || *n > cap || sn > cap - *n) {
        return -1;
    }
    if (sn > 0) {
        if (src == NULL) {
            return -1;
        }
        memcpy(out + *n, src, sn);
    }
    *n += sn;
    return 0;
}

static size_t line_begin(const char *s, size_t at) {
    while (at > 0 && s[at - 1] != '\n') {
        at--;
    }
    return at;
}

static size_t line_finish(const char *s, size_t len, size_t at) {
    while (at < len && s[at] != '\n') {
        at++;
    }
    return at;
}

static int task_len(const char *s, size_t n) {
    if (n >= 6 && s[0] == '-' && s[1] == ' ' && s[2] == '[' && (s[3] == ' ' || s[3] == 'x' || s[3] == 'X') &&
        s[4] == ']' && s[5] == ' ') {
        return 6;
    }
    return 0;
}

static int bullet_len(const char *s, size_t n) {
    if (n >= 2 && (s[0] == '-' || s[0] == '*' || s[0] == '+') && s[1] == ' ') {
        return 2;
    }
    return 0;
}

static int quote_len(const char *s, size_t n) {
    if (n >= 1 && s[0] == '>') {
        if (n >= 2 && s[1] == ' ') {
            return 2;
        }
        return 1;
    }
    return 0;
}

static int rewrite_line(const char *s, size_t n, int kind, char *out, size_t cap, size_t *wrote) {
    size_t ind = 0;
    while (ind < n && ind < 3 && s[ind] == ' ') {
        ind++;
    }
    size_t w = 0;
    if (buf_add(out, cap, &w, s, ind) != 0) {
        return -1;
    }
    const char *body = s + ind;
    size_t bn = n - ind;
    if (kind == 1) {
        int hashes = 0;
        size_t h = 0;
        while (h < bn && body[h] == '#' && hashes < 6) {
            hashes++;
            h++;
        }
        int head = hashes > 0 && (h == bn || body[h] == ' ' || body[h] == '\t');
        if (!head) {
            if (buf_add(out, cap, &w, "# ", 2) != 0 || buf_add(out, cap, &w, body, bn) != 0) {
                return -1;
            }
        } else if (hashes < 3) {
            char marks[4];
            for (int i = 0; i < hashes + 1; i++) {
                marks[i] = '#';
            }
            marks[hashes + 1] = ' ';
            if (h < bn && (body[h] == ' ' || body[h] == '\t')) {
                h++;
            }
            if (buf_add(out, cap, &w, marks, (size_t)hashes + 2) != 0 || buf_add(out, cap, &w, body + h, bn - h) != 0) {
                return -1;
            }
        } else {
            if (h < bn && (body[h] == ' ' || body[h] == '\t')) {
                h++;
            }
            if (buf_add(out, cap, &w, body + h, bn - h) != 0) {
                return -1;
            }
        }
    } else if (kind == 2) {
        int took = bullet_len(body, bn);
        if (took > 0) {
            if (buf_add(out, cap, &w, body + took, bn - (size_t)took) != 0) {
                return -1;
            }
        } else if (buf_add(out, cap, &w, "- ", 2) != 0 || buf_add(out, cap, &w, body, bn) != 0) {
            return -1;
        }
    } else if (kind == 3) {
        int took = task_len(body, bn);
        int bullet = bullet_len(body, bn);
        if (took > 0) {
            if (buf_add(out, cap, &w, body + took, bn - (size_t)took) != 0) {
                return -1;
            }
        } else if (bullet > 0) {
            if (buf_add(out, cap, &w, "- [ ] ", 6) != 0 || buf_add(out, cap, &w, body + bullet, bn - (size_t)bullet) != 0) {
                return -1;
            }
        } else if (buf_add(out, cap, &w, "- [ ] ", 6) != 0 || buf_add(out, cap, &w, body, bn) != 0) {
            return -1;
        }
    } else if (kind == 4) {
        int took = quote_len(body, bn);
        if (took > 0) {
            if (buf_add(out, cap, &w, body + took, bn - (size_t)took) != 0) {
                return -1;
            }
        } else if (buf_add(out, cap, &w, "> ", 2) != 0 || buf_add(out, cap, &w, body, bn) != 0) {
            return -1;
        }
    } else {
        return -1;
    }
    *wrote = w;
    return 0;
}

int doc_line(Doc *doc, int kind) {
    if (doc == NULL || doc->bytes == NULL || kind < 1 || kind > 4) {
        return -1;
    }
    size_t lo = 0;
    size_t hi = 0;
    sel_bounds(doc, &lo, &hi);
    if (hi < lo || hi > doc->len) {
        return -1;
    }
    size_t start = line_begin(doc->bytes, lo);
    size_t end_at = hi > start ? hi - 1 : hi;
    if (end_at > doc->len) {
        end_at = doc->len;
    }
    size_t end = line_finish(doc->bytes, doc->len, end_at);
    if (end < start || end - start > DOC_MAX_BYTES) {
        return -1;
    }
    size_t lines = 1;
    for (size_t k = start; k < end; k++) {
        if (doc->bytes[k] == '\n') {
            if (lines == SIZE_MAX) {
                return -1;
            }
            lines++;
        }
    }
    if (lines > SIZE_MAX / 8) {
        return -1;
    }
    size_t span = end - start;
    if (span > SIZE_MAX - lines * 8) {
        return -1;
    }
    size_t cap = span + lines * 8;
    if (cap > DOC_MAX_BYTES) {
        return -1;
    }
    char *buf = malloc(cap > 0 ? cap : 1);
    if (buf == NULL) {
        return -1;
    }
    size_t w = 0;
    size_t p = start;
    int rc = 0;
    while (p < end || (p == start && p == end)) {
        size_t e = p;
        while (e < end && doc->bytes[e] != '\n') {
            e++;
        }
        size_t piece = 0;
        if (rewrite_line(doc->bytes + p, e - p, kind, buf + w, cap - w, &piece) != 0) {
            rc = -1;
            break;
        }
        w += piece;
        if (e < end && doc->bytes[e] == '\n') {
            if (buf_add(buf, cap, &w, "\n", 1) != 0) {
                rc = -1;
                break;
            }
            p = e + 1;
            if (p == end) {
                break;
            }
        } else {
            break;
        }
        if (p == start && p == end) {
            break;
        }
    }
    if (rc == 0) {
        rc = edit(doc, start, end - start, buf, w);
    }
    free(buf);
    return rc;
}

int doc_link(Doc *doc) {
    if (doc == NULL || doc->bytes == NULL) {
        return -1;
    }
    size_t lo = 0;
    size_t hi = 0;
    sel_bounds(doc, &lo, &hi);
    if (hi < lo || hi > doc->len || hi - lo > DOC_MAX_BYTES - 4) {
        return -1;
    }
    size_t sel = hi - lo;
    size_t n = sel + 4;
    char *buf = malloc(n);
    if (buf == NULL) {
        return -1;
    }
    buf[0] = '[';
    if (sel > 0) {
        memcpy(buf + 1, doc->bytes + lo, sel);
    }
    buf[1 + sel] = ']';
    buf[2 + sel] = '(';
    buf[3 + sel] = ')';
    int rc = edit(doc, lo, sel, buf, n);
    free(buf);
    if (rc == 0) {
        doc->caret = sel == 0 ? lo + 1 : lo + sel + 3;
        doc->anchor = doc->caret;
    }
    return rc;
}

int doc_block(Doc *doc, int kind) {
    if (doc == NULL || doc->bytes == NULL || kind < 1 || kind > 3) {
        return -1;
    }
    size_t lo = 0;
    size_t hi = 0;
    sel_bounds(doc, &lo, &hi);
    if (hi < lo || hi > doc->len) {
        return -1;
    }
    const char *body = "---\n";
    size_t body_n = 4;
    size_t place = 4;
    if (kind == 2) {
        body = "|  |  |\n| --- | --- |\n|  |  |\n";
        body_n = 30;
        place = 2;
    } else if (kind == 3) {
        body = "```\n\n```\n";
        body_n = 9;
        place = 4;
    }
    int lead = lo == hi && lo > 0 && doc->bytes[lo - 1] != '\n';
    if (body_n > 96 || (size_t)lead + body_n > 96) {
        return -1;
    }
    char buf[96];
    size_t w = 0;
    if (lead) {
        buf[w++] = '\n';
    }
    memcpy(buf + w, body, body_n);
    w += body_n;
    int rc = edit(doc, lo, hi - lo, buf, w);
    if (rc == 0) {
        size_t at = lo + (size_t)lead + place;
        if (at > lo + w) {
            at = lo + w;
        }
        doc->caret = at;
        doc->anchor = at;
    }
    return rc;
}

static int word_cp(unsigned cp) {
    return cp != ' ' && cp != '\t' && cp != '\n' && cp != '\r';
}

int doc_word_at(const char *text, size_t len, size_t byte_off, size_t *lo, size_t *hi) {
    if (lo == NULL || hi == NULL) {
        return -1;
    }
    if (text == NULL) {
        text = "";
        len = 0;
    }
    if (byte_off > len) {
        byte_off = len;
    }
    while (byte_off > 0 && byte_off < len && ((unsigned char)text[byte_off] & 0xc0) == 0x80) {
        byte_off--;
    }
    size_t here = byte_off;
    unsigned cp = 0;
    int n = byte_off < len ? utf8_decode(text + byte_off, len - byte_off, &cp) : -1;
    if (n < 1 || !word_cp(cp)) {
        if (byte_off == 0) {
            return -1;
        }
        size_t p = byte_off;
        do {
            p--;
        } while (p > 0 && ((unsigned char)text[p] & 0xc0) == 0x80);
        n = utf8_decode(text + p, len - p, &cp);
        if (n < 1 || !word_cp(cp)) {
            return -1;
        }
        here = p;
    }
    size_t start = here;
    while (start > 0) {
        size_t p = start;
        do {
            p--;
        } while (p > 0 && ((unsigned char)text[p] & 0xc0) == 0x80);
        unsigned prev = 0;
        int pn = utf8_decode(text + p, len - p, &prev);
        if (pn < 1 || !word_cp(prev)) {
            break;
        }
        start = p;
    }
    size_t end = here;
    while (end < len) {
        unsigned cur = 0;
        int cn = utf8_decode(text + end, len - end, &cur);
        if (cn < 1 || !word_cp(cur)) {
            break;
        }
        end += (size_t)cn;
    }
    if (end <= start) {
        return -1;
    }
    *lo = start;
    *hi = end;
    return 0;
}

int doc_select_word(Doc *doc) {
    if (doc == NULL || doc->bytes == NULL) {
        return -1;
    }
    if (doc->caret != doc->anchor) {
        return 0;
    }
    size_t lo = 0;
    size_t hi = 0;
    if (doc_word_at(doc->bytes, doc->len, doc->caret, &lo, &hi) != 0) {
        return -1;
    }
    doc->anchor = lo;
    doc->caret = hi;
    return 0;
}

int doc_backspace(Doc *doc) {
    size_t lo = 0;
    size_t hi = 0;
    sel_bounds(doc, &lo, &hi);
    if (lo != hi) {
        return edit(doc, lo, hi - lo, NULL, 0);
    }
    if (doc->caret == 0) {
        return -1;
    }
    size_t i = doc->caret - 1;
    while (i > 0 && ((unsigned char)doc->bytes[i] & 0xc0) == 0x80) {
        i--;
    }
    return edit(doc, i, doc->caret - i, NULL, 0);
}

int doc_delete(Doc *doc) {
    size_t lo = 0;
    size_t hi = 0;
    sel_bounds(doc, &lo, &hi);
    if (lo != hi) {
        return edit(doc, lo, hi - lo, NULL, 0);
    }
    if (doc->caret >= doc->len) {
        return -1;
    }
    unsigned cp = 0;
    int n = utf8_decode(doc->bytes + doc->caret, doc->len - doc->caret, &cp);
    if (n < 0) {
        return -1;
    }
    return edit(doc, doc->caret, (size_t)n, NULL, 0);
}

int doc_undo(Doc *doc) {
    if (doc->undo_n == 0) {
        return -1;
    }
    struct Edit e = doc->undo[--doc->undo_n];
    if (doc->undo_bytes >= e.n_old) {
        doc->undo_bytes -= e.n_old;
    } else {
        doc->undo_bytes = 0;
    }
    int rc = splice(doc, e.at, e.n_new, e.old, e.n_old);
    if (rc != 0) {
        free(e.old);
        return -1;
    }
    doc->caret = e.caret;
    doc->anchor = e.anchor;
    free(e.old);
    return 0;
}

static void move_caret(Doc *doc, int codepoints) {
    if (codepoints > 0) {
        while (codepoints > 0 && doc->caret < doc->len) {
            unsigned cp = 0;
            int n = utf8_decode(doc->bytes + doc->caret, doc->len - doc->caret, &cp);
            if (n < 0) {
                break;
            }
            doc->caret += (size_t)n;
            codepoints--;
        }
    } else {
        while (codepoints < 0 && doc->caret > 0) {
            size_t i = doc->caret - 1;
            while (i > 0 && ((unsigned char)doc->bytes[i] & 0xc0) == 0x80) {
                i--;
            }
            doc->caret = i;
            codepoints++;
        }
    }
}

void doc_move(Doc *doc, int codepoints) {
    move_caret(doc, codepoints);
    doc->anchor = doc->caret;
}

static size_t snap_off(const Doc *doc, size_t byte_off) {
    if (byte_off > doc->len) {
        byte_off = doc->len;
    }
    while (byte_off > 0 && byte_off < doc->len && ((unsigned char)doc->bytes[byte_off] & 0xc0) == 0x80) {
        byte_off--;
    }
    return byte_off;
}

void doc_place(Doc *doc, size_t byte_off) {
    byte_off = snap_off(doc, byte_off);
    doc->caret = byte_off;
    doc->anchor = doc->caret;
}

void doc_seek(Doc *doc, size_t byte_off) {
    doc->caret = snap_off(doc, byte_off);
}

void doc_select_all(Doc *doc) {
    doc->anchor = 0;
    doc->caret = doc->len;
}

int doc_toggle_task(Doc *doc, size_t byte_off) {
    if (doc == NULL || byte_off > doc->len) {
        return -1;
    }
    size_t start = byte_off;
    while (start > 0 && doc->bytes[start - 1] != '\n') {
        start--;
    }
    size_t i = start;
    size_t limit = start + 24;
    if (limit > doc->len) {
        limit = doc->len;
    }
    while (i < limit && doc->bytes[i] == ' ') {
        i++;
    }
    if (i + 4 >= doc->len) {
        return -1;
    }
    char mark = doc->bytes[i];
    if (!((mark == '-' || mark == '+' || mark == '*') && doc->bytes[i + 1] == ' ' && doc->bytes[i + 2] == '[')) {
        return -1;
    }
    size_t box = i + 3;
    if (box + 1 >= doc->len || doc->bytes[box + 1] != ']') {
        return -1;
    }
    char c = doc->bytes[box];
    if (c != ' ' && c != 'x' && c != 'X') {
        return -1;
    }
    if (byte_off > box + 1) {
        return -1;
    }
    char next = c == ' ' ? 'x' : ' ';
    return splice(doc, box, 1, &next, 1);
}

static int line_has_pipe(const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '|') {
            return 1;
        }
    }
    return 0;
}

static int cell_is_delim(const char *s, size_t n) {
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
    if (s[i] == ':') {
        i++;
    }
    size_t dashes = 0;
    while (i < end && s[i] == '-') {
        dashes++;
        i++;
    }
    if (i < end && s[i] == ':') {
        i++;
    }
    return i == end && dashes >= 1;
}

static int row_is_delim(const char *s, size_t n, int *cols) {
    if (!line_has_pipe(s, n)) {
        return 0;
    }
    int count = 0;
    int any = 0;
    size_t i = 0;
    while (i < n && s[i] != '|') {
        i++;
    }
    while (i < n) {
        i++;
        size_t start = i;
        while (i < n && s[i] != '|') {
            i++;
        }
        if (i == n && start == i) {
            break;
        }
        if (!cell_is_delim(s + start, i - start)) {
            return 0;
        }
        any = 1;
        count++;
        if (i >= n) {
            break;
        }
    }
    if (!any) {
        return 0;
    }
    if (cols != NULL) {
        *cols = count;
    }
    return 1;
}

static int cell_starts(const char *s, size_t n, size_t *out, int cap) {
    int count = 0;
    size_t i = 0;
    while (i < n && s[i] != '|') {
        i++;
    }
    while (i < n && count < cap) {
        i++;
        if (i >= n) {
            break;
        }
        if (s[i] == ' ') {
            i++;
        }
        out[count++] = i;
        while (i < n && s[i] != '|') {
            i++;
        }
    }
    return count;
}

/* Enter in a cell moves to the same column on the next row. The last row grows by one. */
static int table_down(Doc *doc) {
    size_t caret = 0;
    size_t line = 0;
    size_t lend = 0;
    size_t first = 0;
    size_t last = 0;
    size_t last_end = 0;
    size_t scan = 0;
    int cols = 0;
    int saw_delim = 0;
    int on_delim = 0;
    int index = 0;
    size_t starts[8];
    int ncells = 0;
    if (doc == NULL || doc->bytes == NULL) {
        return -1;
    }
    caret = doc->caret;
    if (caret > doc->len) {
        caret = doc->len;
    }
    line = caret;
    while (line > 0 && doc->bytes[line - 1] != '\n') {
        line--;
    }
    lend = line;
    while (lend < doc->len && doc->bytes[lend] != '\n') {
        lend++;
    }
    if (!line_has_pipe(doc->bytes + line, lend - line)) {
        return -1;
    }
    first = line;
    while (first > 0) {
        size_t prev_end = first - 1;
        size_t prev = prev_end;
        while (prev > 0 && doc->bytes[prev - 1] != '\n') {
            prev--;
        }
        if (!line_has_pipe(doc->bytes + prev, prev_end - prev)) {
            break;
        }
        first = prev;
    }
    last = line;
    last_end = lend;
    scan = lend;
    while (scan < doc->len && doc->bytes[scan] == '\n') {
        size_t next = scan + 1;
        size_t next_end = next;
        while (next_end < doc->len && doc->bytes[next_end] != '\n') {
            next_end++;
        }
        if (!line_has_pipe(doc->bytes + next, next_end - next)) {
            break;
        }
        last = next;
        last_end = next_end;
        scan = next_end;
    }
    for (size_t at = first; at <= last;) {
        size_t end = at;
        int n = 0;
        while (end < doc->len && doc->bytes[end] != '\n') {
            end++;
        }
        if (row_is_delim(doc->bytes + at, end - at, &n)) {
            saw_delim = 1;
            cols = n;
        }
        if (at == last) {
            break;
        }
        if (end < doc->len && doc->bytes[end] == '\n') {
            at = end + 1;
        } else {
            break;
        }
    }
    if (!saw_delim || cols < 1) {
        return -1;
    }
    on_delim = row_is_delim(doc->bytes + line, lend - line, NULL);
    if (!on_delim) {
        ncells = cell_starts(doc->bytes + line, lend - line, starts, 8);
        for (int c = 0; c < ncells; c++) {
            if (line + starts[c] <= caret) {
                index = c;
            }
        }
    }
    {
        size_t at = lend < doc->len && doc->bytes[lend] == '\n' ? lend + 1 : doc->len;
        while (at <= last && at < doc->len) {
            size_t end = at;
            size_t land[8];
            int land_n = 0;
            int pick = index;
            while (end < doc->len && doc->bytes[end] != '\n') {
                end++;
            }
            if (!row_is_delim(doc->bytes + at, end - at, NULL)) {
                land_n = cell_starts(doc->bytes + at, end - at, land, 8);
                if (land_n < 1) {
                    return -1;
                }
                if (pick >= land_n) {
                    pick = land_n - 1;
                }
                doc_place(doc, at + land[pick]);
                return 0;
            }
            if (at == last || end >= doc->len || doc->bytes[end] != '\n') {
                break;
            }
            at = end + 1;
        }
    }
    if (cols > 8) {
        cols = 8;
    }
    if (index >= cols) {
        index = cols - 1;
    }
    if (index < 0) {
        index = 0;
    }
    {
        char buf[64];
        size_t n = 0;
        size_t land = 0;
        buf[n++] = '\n';
        for (int c = 0; c < cols; c++) {
            if (n + 2 >= sizeof buf) {
                break;
            }
            buf[n++] = '|';
            buf[n++] = ' ';
        }
        if (n + 1 >= sizeof buf) {
            return -1;
        }
        buf[n++] = '|';
        if (splice(doc, last_end, 0, buf, n) != 0) {
            return -1;
        }
        land = last_end + 2 + (size_t)index * 2;
        if (land > doc->len) {
            land = doc->len;
        }
        doc_place(doc, land);
    }
    return 0;
}

static int pipe_at(const char *s, size_t n, size_t *at, int cap) {
    int count = 0;
    if (s == NULL || at == NULL || cap < 1) {
        return 0;
    }
    for (size_t i = 0; i < n && count < cap; i++) {
        if (s[i] == '|') {
            at[count++] = i;
        }
    }
    return count;
}

int doc_table_column(Doc *doc, int dir) {
    size_t caret = 0;
    size_t line = 0;
    size_t lend = 0;
    size_t first = 0;
    size_t last = 0;
    size_t scan = 0;
    int index = 0;
    int cols = 0;
    int saw = 0;
    size_t pipes[12];
    int np = 0;
    if (doc == NULL || doc->bytes == NULL || (dir != 1 && dir != -1)) {
        return -1;
    }
    caret = doc->caret;
    if (caret > doc->len) {
        caret = doc->len;
    }
    line = caret;
    while (line > 0 && doc->bytes[line - 1] != '\n') {
        line--;
    }
    lend = line;
    while (lend < doc->len && doc->bytes[lend] != '\n') {
        lend++;
    }
    if (!line_has_pipe(doc->bytes + line, lend - line)) {
        return -1;
    }
    np = pipe_at(doc->bytes + line, lend - line, pipes, 12);
    if (np < 2) {
        return -1;
    }
    cols = np - 1;
    for (int c = 0; c + 1 < np; c++) {
        if (line + pipes[c] <= caret) {
            index = c;
        }
    }
    if (index >= cols) {
        index = cols - 1;
    }
    if (dir < 0 && cols < 2) {
        return -1;
    }
    first = line;
    while (first > 0) {
        size_t prev_end = first - 1;
        size_t prev = prev_end;
        while (prev > 0 && doc->bytes[prev - 1] != '\n') {
            prev--;
        }
        if (!line_has_pipe(doc->bytes + prev, prev_end - prev)) {
            break;
        }
        first = prev;
    }
    last = line;
    scan = lend;
    while (scan < doc->len && doc->bytes[scan] == '\n') {
        size_t next = scan + 1;
        size_t next_end = next;
        while (next_end < doc->len && doc->bytes[next_end] != '\n') {
            next_end++;
        }
        if (!line_has_pipe(doc->bytes + next, next_end - next)) {
            break;
        }
        last = next;
        scan = next_end;
    }
    /* Edit the bottom row first so the rows above keep their offsets. */
    scan = last;
    while (!saw || scan >= first) {
        size_t end = scan;
        size_t row_pipes[12];
        int row_n = 0;
        int delim_n = 0;
        int is_delim = 0;
        while (end < doc->len && doc->bytes[end] != '\n') {
            end++;
        }
        is_delim = row_is_delim(doc->bytes + scan, end - scan, &delim_n);
        row_n = pipe_at(doc->bytes + scan, end - scan, row_pipes, 12);
        if (row_n >= index + 2) {
            if (dir > 0) {
                const char *insert = is_delim ? " --- |" : " |";
                size_t n = is_delim ? 6 : 2;
                size_t at = scan + row_pipes[index + 1] + 1;
                if (edit(doc, at, 0, insert, n) != 0) {
                    return -1;
                }
            } else if (index + 1 < row_n) {
                size_t from = scan + row_pipes[index];
                size_t to = scan + row_pipes[index + 1];
                if (to > from && edit(doc, from, to - from, "", 0) != 0) {
                    return -1;
                }
            }
        }
        if (scan == first) {
            break;
        }
        if (scan == 0) {
            break;
        }
        scan--;
        while (scan > 0 && doc->bytes[scan - 1] != '\n') {
            scan--;
        }
        saw = 1;
    }
    (void)cols;
    return 0;
}

int doc_table_tab(Doc *doc, int dir) {
    if (doc == NULL || (dir != 1 && dir != -1) || doc->bytes == NULL) {
        return -1;
    }
    size_t caret = doc->caret;
    if (caret > doc->len) {
        caret = doc->len;
    }
    size_t line = caret;
    while (line > 0 && doc->bytes[line - 1] != '\n') {
        line--;
    }
    size_t lend = line;
    while (lend < doc->len && doc->bytes[lend] != '\n') {
        lend++;
    }
    if (!line_has_pipe(doc->bytes + line, lend - line)) {
        return -1;
    }
    size_t first = line;
    while (first > 0) {
        size_t prev_end = first - 1;
        size_t prev = prev_end;
        while (prev > 0 && doc->bytes[prev - 1] != '\n') {
            prev--;
        }
        if (!line_has_pipe(doc->bytes + prev, prev_end - prev)) {
            break;
        }
        first = prev;
    }
    size_t last = line;
    size_t last_end = lend;
    size_t scan = lend;
    while (scan < doc->len && doc->bytes[scan] == '\n') {
        size_t next = scan + 1;
        size_t next_end = next;
        while (next_end < doc->len && doc->bytes[next_end] != '\n') {
            next_end++;
        }
        if (!line_has_pipe(doc->bytes + next, next_end - next)) {
            break;
        }
        last = next;
        last_end = next_end;
        scan = next_end;
    }
    int cols = 0;
    int saw_delim = 0;
    for (size_t at = first; at <= last;) {
        size_t end = at;
        while (end < doc->len && doc->bytes[end] != '\n') {
            end++;
        }
        int n = 0;
        if (row_is_delim(doc->bytes + at, end - at, &n)) {
            saw_delim = 1;
            cols = n;
        }
        if (at == last) {
            break;
        }
        if (end < doc->len && doc->bytes[end] == '\n') {
            at = end + 1;
        } else {
            break;
        }
    }
    if (!saw_delim || cols < 1) {
        return -1;
    }
    int on_delim = row_is_delim(doc->bytes + line, lend - line, NULL);
    size_t starts[8];
    int ncells = 0;
    if (!on_delim) {
        ncells = cell_starts(doc->bytes + line, lend - line, starts, 8);
        for (int c = 0; c < ncells; c++) {
            starts[c] += line;
        }
    }
    int index = -1;
    if (!on_delim) {
        for (int c = 0; c < ncells; c++) {
            if (starts[c] <= caret) {
                index = c;
            }
        }
    }
    if (dir > 0 && !on_delim && index + 1 < ncells) {
        doc_place(doc, starts[index + 1]);
        return 0;
    }
    if (dir < 0 && !on_delim && index > 0) {
        doc_place(doc, starts[index - 1]);
        return 0;
    }
    size_t neighbor = line;
    size_t neighbor_end = lend;
    if (dir > 0) {
        size_t at = lend < doc->len && doc->bytes[lend] == '\n' ? lend + 1 : doc->len;
        while (at <= last) {
            size_t end = at;
            while (end < doc->len && doc->bytes[end] != '\n') {
                end++;
            }
            if (!row_is_delim(doc->bytes + at, end - at, NULL)) {
                neighbor = at;
                neighbor_end = end;
                break;
            }
            if (at == last) {
                neighbor = doc->len;
                break;
            }
            if (end < doc->len && doc->bytes[end] == '\n') {
                at = end + 1;
            } else {
                neighbor = doc->len;
                break;
            }
        }
        if (neighbor == line) {
            neighbor = doc->len;
        }
    } else {
        size_t at = first;
        size_t prev_body = doc->len;
        size_t prev_end = 0;
        while (at < line) {
            size_t end = at;
            while (end < doc->len && doc->bytes[end] != '\n') {
                end++;
            }
            if (!row_is_delim(doc->bytes + at, end - at, NULL)) {
                prev_body = at;
                prev_end = end;
            }
            if (end < doc->len && doc->bytes[end] == '\n') {
                at = end + 1;
            } else {
                break;
            }
        }
        if (prev_body == doc->len) {
            return -1;
        }
        neighbor = prev_body;
        neighbor_end = prev_end;
    }
    if (dir > 0 && neighbor == doc->len) {
        if (cols > 8) {
            cols = 8;
        }
        char buf[64];
        size_t n = 0;
        buf[n++] = '\n';
        for (int c = 0; c < cols; c++) {
            if (n + 2 >= sizeof buf) {
                break;
            }
            buf[n++] = '|';
            buf[n++] = ' ';
        }
        if (n + 1 >= sizeof buf) {
            return -1;
        }
        buf[n++] = '|';
        if (splice(doc, last_end, 0, buf, n) != 0) {
            return -1;
        }
        doc_place(doc, last_end + 2);
        return 1;
    }
    size_t land[8];
    int land_n = cell_starts(doc->bytes + neighbor, neighbor_end - neighbor, land, 8);
    if (land_n < 1) {
        return -1;
    }
    int pick = dir > 0 ? 0 : land_n - 1;
    doc_place(doc, neighbor + land[pick]);
    return 0;
}

void doc_extend(Doc *doc, int codepoints) {
    move_caret(doc, codepoints);
}

static size_t line_start(const Doc *doc, size_t index) {
    while (index > 0 && doc->bytes[index - 1] != '\n') {
        index--;
    }
    return index;
}

static size_t line_end(const Doc *doc, size_t index) {
    while (index < doc->len && doc->bytes[index] != '\n') {
        index++;
    }
    return index;
}

static int column_at(const Doc *doc, size_t start, size_t caret) {
    int column = 0;
    size_t i = start;
    while (i < caret && i < doc->len) {
        unsigned cp = 0;
        int n = utf8_decode(doc->bytes + i, doc->len - i, &cp);
        if (n < 0) {
            break;
        }
        i += (size_t)n;
        column++;
    }
    return column;
}

static size_t offset_at_column(const Doc *doc, size_t start, int column) {
    size_t i = start;
    int seen = 0;
    while (seen < column && i < doc->len && doc->bytes[i] != '\n') {
        unsigned cp = 0;
        int n = utf8_decode(doc->bytes + i, doc->len - i, &cp);
        if (n < 0) {
            break;
        }
        i += (size_t)n;
        seen++;
    }
    return i;
}

/* shortcut: rows are newline-separated, not visual wraps. */
static void move_row(Doc *doc, int dir) {
    if (dir == 0) {
        return;
    }
    size_t start = line_start(doc, doc->caret);
    int column = column_at(doc, start, doc->caret);
    if (dir < 0) {
        if (start == 0) {
            doc->caret = 0;
            return;
        }
        size_t prev = line_start(doc, start - 1);
        doc->caret = offset_at_column(doc, prev, column);
        return;
    }
    size_t end = line_end(doc, start);
    if (end >= doc->len) {
        doc->caret = doc->len;
        return;
    }
    doc->caret = offset_at_column(doc, end + 1, column);
}

void doc_move_row(Doc *doc, int dir) {
    move_row(doc, dir);
    doc->anchor = doc->caret;
}

void doc_extend_row(Doc *doc, int dir) {
    move_row(doc, dir);
}

int doc_load_bytes(Doc *doc, const char *bytes, size_t n) {
    if (doc == NULL || (bytes == NULL && n != 0) || n > DOC_MAX_BYTES || (n > 0 && !utf8_valid(bytes, n))) {
        return -1;
    }
    char *copy = calloc(1, n + 1);
    if (copy == NULL) {
        return -1;
    }
    if (n > 0) {
        memcpy(copy, bytes, n);
    }
    struct Piece *piece = NULL;
    size_t npiece = 0;
    size_t cap = 0;
    size_t off = 0;
    int built = 0;
    do {
        size_t chunk = n - off;
        if (chunk > ROPE_LEAF) {
            chunk = ROPE_LEAF;
        }
        if (npiece == cap) {
            size_t next_cap = cap == 0 ? 4 : cap * 2;
            struct Piece *grown = realloc(piece, next_cap * sizeof *grown);
            if (grown == NULL) {
                built = -1;
                break;
            }
            piece = grown;
            cap = next_cap;
        }
        char *data = malloc(chunk > 0 ? chunk : 1);
        if (data == NULL) {
            built = -1;
            break;
        }
        if (chunk > 0) {
            memcpy(data, bytes + off, chunk);
        }
        piece[npiece].data = data;
        piece[npiece].len = chunk;
        piece[npiece].cap = chunk > 0 ? chunk : 1;
        npiece++;
        off += chunk;
    } while (off < n);
    if (built != 0 || npiece == 0) {
        for (size_t i = 0; i < npiece; i++) {
            free(piece[i].data);
        }
        free(piece);
        free(copy);
        return -1;
    }
    piece_clear(doc);
    free(doc->bytes);
    doc->piece = piece;
    doc->npiece = npiece;
    doc->piece_cap = cap;
    doc->bytes = copy;
    doc->len = n;
    doc->cap = n + 1;
    doc->caret = n;
    doc->anchor = n;
    undo_clear(doc);
    doc->dirty_at = 0;
    doc->saved_len = SIZE_MAX;
    doc->saved_path[0] = '\0';
    return 0;
}

int doc_load(Doc *doc, const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return -1;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return -1;
    }
    long size = ftell(file);
    if (size < 0 || (unsigned long)size > DOC_MAX_BYTES) {
        fclose(file);
        return -1;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return -1;
    }
    char *bytes = malloc((size_t)size + 1);
    if (bytes == NULL) {
        fclose(file);
        return -1;
    }
    size_t n = fread(bytes, 1, (size_t)size, file);
    fclose(file);
    int rc = n == (size_t)size ? doc_load_bytes(doc, bytes, n) : -1;
    free(bytes);
    if (rc == 0 && strlen(path) < sizeof doc->saved_path) {
        memcpy(doc->saved_path, path, strlen(path) + 1);
        doc->saved_len = doc->len;
        doc->dirty_at = SIZE_MAX;
    }
    return rc;
}

static int file_len(const char *path, size_t *out) {
    FILE *file = fopen(path, "rb");
    long size = 0;
    if (file == NULL) {
        return -1;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return -1;
    }
    size = ftell(file);
    fclose(file);
    if (size < 0) {
        return -1;
    }
    *out = (size_t)size;
    return 0;
}

static int same_save(const Doc *doc, const char *path) {
    return doc->saved_path[0] != '\0' && strcmp(doc->saved_path, path) == 0 && doc->saved_len != SIZE_MAX;
}

static void remember_save(Doc *doc, const char *path) {
    size_t n = strlen(path);
    if (n >= sizeof doc->saved_path) {
        doc->saved_path[0] = '\0';
        doc->dirty_at = 0;
        return;
    }
    memcpy(doc->saved_path, path, n + 1);
    doc->saved_len = doc->len;
    doc->dirty_at = SIZE_MAX;
}

static int save_full(Doc *doc, const char *path) {
    size_t path_len = strlen(path);
    char *tmp = malloc(path_len + 5);
    FILE *file = NULL;
    size_t n = 0;
    int closed = 0;
    if (tmp == NULL) {
        return -1;
    }
    memcpy(tmp, path, path_len);
    memcpy(tmp + path_len, ".tmp", 5);
    file = fopen(tmp, "wb");
    if (file == NULL) {
        free(tmp);
        return -1;
    }
    n = fwrite(doc->bytes, 1, doc->len, file);
    closed = fclose(file);
    if (n != doc->len || closed != 0) {
        remove(tmp);
        free(tmp);
        return -1;
    }
    if (rename(tmp, path) != 0) {
        remove(tmp);
        free(tmp);
        return -1;
    }
    free(tmp);
    return 0;
}

/* 0 appends the new tail. 1 shortens the file. The prefix is already the saved bytes. */
static int save_end(Doc *doc, const char *path, int shorten) {
    size_t have = 0;
    FILE *file = NULL;
    if (file_len(path, &have) != 0 || have != doc->saved_len) {
        return -1;
    }
    file = fopen(path, "rb+");
    if (file == NULL) {
        return -1;
    }
    if (shorten) {
        if (ftruncate(fileno(file), (off_t)doc->len) != 0) {
            fclose(file);
            return -1;
        }
    } else {
        size_t n = doc->len - doc->saved_len;
        size_t wrote = 0;
        if (fseek(file, (long)doc->saved_len, SEEK_SET) != 0) {
            fclose(file);
            return -1;
        }
        wrote = fwrite(doc->bytes + doc->saved_len, 1, n, file);
        if (wrote != n || fflush(file) != 0) {
            fclose(file);
            return -1;
        }
    }
    if (fclose(file) != 0) {
        return -1;
    }
    return 0;
}

int doc_save(Doc *doc, const char *path) {
    int append = 0;
    int shorten = 0;
    if (doc == NULL || path == NULL) {
        return -1;
    }
    if (doc->dirty_at == SIZE_MAX && same_save(doc, path) && doc->saved_len == doc->len) {
        return 0;
    }
    append = same_save(doc, path) && doc->dirty_at == doc->saved_len && doc->len > doc->saved_len;
    shorten = same_save(doc, path) && doc->dirty_at == doc->len && doc->len < doc->saved_len;
    if (append && save_end(doc, path, 0) == 0) {
        remember_save(doc, path);
        return 0;
    }
    if (shorten && save_end(doc, path, 1) == 0) {
        remember_save(doc, path);
        return 0;
    }
    if (save_full(doc, path) != 0) {
        return -1;
    }
    remember_save(doc, path);
    return 0;
}
