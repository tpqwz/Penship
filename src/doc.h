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

/* Wrap the selection in mark, or remove that wrap when it is already there.
   An empty selection inserts the mark, the caret, and the mark again.
   mark is a short UTF-8 string such as "**", "*", "~~", or "`", at most 8 bytes.
   Returns 0, or -1. */
int doc_mark(Doc *doc, const char *mark, size_t n);

/* Turn the selection into a link. The caret ends between () when text was selected, and inside [] when it was not. */
int doc_link(Doc *doc);

/* Toggle a mark on each line the selection touches.
   1 cycles a heading through #, ##, ###, then off. 2 is a bullet. 3 is a task. 4 is a quote. */
int doc_line(Doc *doc, int kind);

/* Insert a block at the caret. 1 is a rule, 2 is a table, 3 is a fenced block. The caret ends inside it. */
int doc_block(Doc *doc, int kind);

/* The word around byte_off: a run of characters that are not space, tab, or newline.
   Returns 0 and sets [lo, hi), or -1 when byte_off is not in a word. */
int doc_word_at(const char *text, size_t len, size_t byte_off, size_t *lo, size_t *hi);

/* If nothing is selected, select the word around the caret. Returns 0, or -1 when the caret is not in a word. */
int doc_select_word(Doc *doc);

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

/* Flip the checkbox when byte_off lies on a task marker. Returns 0, or -1. */
int doc_toggle_task(Doc *doc, size_t byte_off);

/* Move to the next or previous table cell. dir is 1 or -1.
   Tab off the last cell adds an empty row and returns 1. A move returns 0.
   Returns -1 when the caret is not in a table. */
int doc_table_tab(Doc *doc, int dir);

/* 1 adds a column after the caret cell. -1 removes the caret column when another remains. */
int doc_table_column(Doc *doc, int dir);

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
