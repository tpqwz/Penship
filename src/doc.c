#include "doc.h"

#include "utf8.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

struct Doc {
    char *bytes;
    size_t len;
    size_t cap;
    size_t caret;
    size_t anchor;
    struct Edit undo[DOC_UNDO_MAX];
    size_t undo_n;
    size_t undo_bytes;
};

/* shortcut: memmove on every insert, gap buffer when notes get large. */

Doc *doc_new(void) {
    Doc *doc = calloc(1, sizeof *doc);
    if (doc == NULL) {
        return NULL;
    }
    doc->bytes = calloc(1, 1);
    if (doc->bytes == NULL) {
        free(doc);
        return NULL;
    }
    doc->cap = 1;
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

static int grow(Doc *doc, size_t need) {
    if (need <= doc->cap) {
        return 0;
    }
    size_t cap = doc->cap < 64 ? 64 : doc->cap;
    while (cap < need) {
        if (cap > (SIZE_MAX / 2)) {
            return -1;
        }
        cap *= 2;
    }
    char *bytes = realloc(doc->bytes, cap);
    if (bytes == NULL) {
        return -1;
    }
    doc->bytes = bytes;
    doc->cap = cap;
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

static int splice(Doc *doc, size_t at, size_t cut, const char *in, size_t n) {
    if (at > doc->len || cut > doc->len - at || n > SIZE_MAX - (doc->len - cut) - 1) {
        return -1;
    }
    if (grow(doc, doc->len - cut + n + 1) != 0) {
        return -1;
    }
    memmove(doc->bytes + at + n, doc->bytes + at + cut, doc->len - at - cut + 1);
    if (n > 0) {
        memcpy(doc->bytes + at, in, n);
    }
    doc->len = doc->len - cut + n;
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

int doc_insert(Doc *doc, const char *utf8, size_t n) {
    if (n == 0) {
        return 0;
    }
    size_t lo = 0;
    size_t hi = 0;
    sel_bounds(doc, &lo, &hi);
    return edit(doc, lo, hi - lo, utf8, n);
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
    free(doc->bytes);
    doc->bytes = copy;
    doc->len = n;
    doc->cap = n + 1;
    doc->caret = n;
    doc->anchor = n;
    undo_clear(doc);
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
    return rc;
}

int doc_save(const Doc *doc, const char *path) {
    size_t path_len = strlen(path);
    char *tmp = malloc(path_len + 5);
    if (tmp == NULL) {
        return -1;
    }
    memcpy(tmp, path, path_len);
    memcpy(tmp + path_len, ".tmp", 5);
    FILE *file = fopen(tmp, "wb");
    if (file == NULL) {
        free(tmp);
        return -1;
    }
    size_t n = fwrite(doc->bytes, 1, doc->len, file);
    int closed = fclose(file);
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
