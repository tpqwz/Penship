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
