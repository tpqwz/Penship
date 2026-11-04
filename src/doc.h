#ifndef PENSHIP_DOC_H
#define PENSHIP_DOC_H

#include <stddef.h>

typedef struct Doc Doc;

Doc *doc_new(void);
void doc_free(Doc *doc);

/* Bytes stay valid until the next edit. *len is the byte count, not counting a trailing NUL. */
const char *doc_text(const Doc *doc, size_t *len);
size_t doc_caret(const Doc *doc);

/* The other end of the selection. Equal to the caret when nothing is selected. */
size_t doc_anchor(const Doc *doc);

/* Insert UTF-8, replacing the selection. Returns 0, or -1 if utf8 is invalid or allocation fails. */
int doc_insert(Doc *doc, const char *utf8, size_t n);

/* Delete the selection, or the code point before the caret. Returns 0, or -1 if nothing can be deleted. */
int doc_backspace(Doc *doc);

/* Delete the selection, or the code point after the caret. Returns 0, or -1 if nothing can be deleted. */
int doc_delete(Doc *doc);

/* Move the caret by code points and collapse the selection. Stops at either end. */
void doc_move(Doc *doc, int codepoints);

/* Put the caret and anchor on a byte offset, snapped back to a code-point boundary. */
void doc_place(Doc *doc, size_t byte_off);

/* Move only the caret, so a drag can grow the selection. Same snap as doc_place. */
void doc_seek(Doc *doc, size_t byte_off);

/* Select the whole document. */
void doc_select_all(Doc *doc);

/* Move the caret and keep the anchor, so the selection grows or shrinks. */
void doc_extend(Doc *doc, int codepoints);

/* Move to the previous (dir < 0) or next (dir > 0) newline row, keeping the code-point column.
   doc_move_row collapses the selection. doc_extend_row keeps the anchor. */
void doc_move_row(Doc *doc, int dir);
void doc_extend_row(Doc *doc, int dir);

/* Reverse the newest recorded edit. Returns 0, or -1 when the stack is empty.
   At most 32 edits are kept. A single deleted span larger than 64 KiB is not recorded. */
int doc_undo(Doc *doc);

/* 4 MiB. A larger buffer is refused so a hostile open cannot replace the document. */
#define DOC_MAX_BYTES (1u << 22)

/* Replace the document with bytes. n == 0 yields an empty document.
   A null pointer is allowed only when n is 0. More than DOC_MAX_BYTES, or bytes that
   are not UTF-8, returns -1 and leaves doc unchanged. The caret ends at the end. */
int doc_load_bytes(Doc *doc, const char *bytes, size_t n);

/* Replace the document with the file. A missing file returns -1 and leaves doc unchanged.
   The caret ends at the end of the loaded text. */
int doc_load(Doc *doc, const char *path);

/* Write the document to path by renaming a temporary file next to it. */
int doc_save(const Doc *doc, const char *path);

#endif
