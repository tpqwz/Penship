#ifndef PENSHIP_IME_H
#define PENSHIP_IME_H

#include <stddef.h>

enum { IME_TEXT = 1, IME_ERASE = 2, IME_FINISH = 3, IME_UNDO = 4, IME_SHORTCUT = 5, IME_FORWARD = 6, IME_MOVE = 7,
       IME_ROW = 8, IME_SELECT = 9, IME_COPY = 10, IME_CUT = 11, IME_PASTE = 12 };

/* A commit longer than this is rejected. */
enum { IME_TEXT_MAX = 4096 };

struct ImeOp {
    int kind;
    int composing;
    int count;
    char *bytes;
    size_t n;
};

/* Queue a commit or a composition. composing replaces the open composition. Returns 0, or -1. */
int ime_push_text(const char *utf8, size_t n, int composing);

/* Queue a backspace of count code points. Returns 0, or -1. */
int ime_push_erase(int count);

/* Queue a forward delete of count code points. Returns 0, or -1. */
int ime_push_forward(int count);

/* Queue a one-code-point move. dir is -1 or 1. extend keeps the anchor. Returns 0, or -1. */
int ime_push_move(int dir, int extend);

/* Queue a one-row move. dir is -1 or 1. extend keeps the anchor. Returns 0, or -1. */
int ime_push_row(int dir, int extend);

/* The open composition becomes ordinary text. */
int ime_push_finish(void);

/* Queue one undo. Returns 0, or -1 when the queue is full. */
int ime_push_undo(void);

/* Queue a chrome shortcut. code must be 1..255. Returns 0, or -1. */
int ime_push_shortcut(int code);

/* Queue select-all, copy, cut, or paste. Returns 0, or -1. */
int ime_push_clip(int kind);

/* Take the oldest op. The caller frees bytes. Returns 0, or -1 when empty. */
int ime_take(struct ImeOp *out);

void ime_reset(void);

#endif
