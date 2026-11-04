#include "ime.h"

#include "utf8.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define IME_CAP 32

static struct ImeOp g_ops[IME_CAP];
static int g_head;
static int g_count;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;

static void free_op(struct ImeOp *op) {
    free(op->bytes);
    op->bytes = NULL;
    op->n = 0;
}

void ime_reset(void) {
    pthread_mutex_lock(&g_mu);
    for (int i = 0; i < g_count; i++) {
        free_op(&g_ops[(g_head + i) % IME_CAP]);
    }
    g_head = 0;
    g_count = 0;
    pthread_mutex_unlock(&g_mu);
}

static int push_op(struct ImeOp op) {
    pthread_mutex_lock(&g_mu);
    int rc = -1;
    if (g_count < IME_CAP) {
        g_ops[(g_head + g_count) % IME_CAP] = op;
        g_count++;
        rc = 0;
    } else {
        free(op.bytes);
    }
    pthread_mutex_unlock(&g_mu);
    return rc;
}

int ime_push_text(const char *utf8, size_t n, int composing) {
    if (n > IME_TEXT_MAX || (n > 0 && (utf8 == NULL || !utf8_valid(utf8, n)))) {
        return -1;
    }
    char *copy = NULL;
    if (n > 0) {
        copy = malloc(n);
        if (copy == NULL) {
            return -1;
        }
        memcpy(copy, utf8, n);
    }
    struct ImeOp op;
    op.kind = IME_TEXT;
    op.composing = composing ? 1 : 0;
    op.count = 0;
    op.bytes = copy;
    op.n = n;
    return push_op(op);
}

static int push_plain(int kind, int composing, int count) {
    struct ImeOp op;
    op.kind = kind;
    op.composing = composing;
    op.count = count;
    op.bytes = NULL;
    op.n = 0;
    return push_op(op);
}

int ime_push_erase(int count) {
    if (count <= 0 || count > 256) {
        return -1;
    }
    return push_plain(IME_ERASE, 0, count);
}

int ime_push_forward(int count) {
    if (count <= 0 || count > 256) {
        return -1;
    }
    return push_plain(IME_FORWARD, 0, count);
}

int ime_push_move(int dir, int extend) {
    if (dir != -1 && dir != 1) {
        return -1;
    }
    return push_plain(IME_MOVE, extend ? 1 : 0, dir);
}

int ime_push_row(int dir, int extend) {
    if (dir != -1 && dir != 1) {
        return -1;
    }
    return push_plain(IME_ROW, extend ? 1 : 0, dir);
}

int ime_push_finish(void) {
    return push_plain(IME_FINISH, 0, 0);
}

int ime_push_undo(void) {
    return push_plain(IME_UNDO, 0, 0);
}

int ime_push_clip(int kind) {
    if (kind != IME_SELECT && kind != IME_COPY && kind != IME_CUT && kind != IME_PASTE) {
        return -1;
    }
    return push_plain(kind, 0, 0);
}

int ime_push_shortcut(int code) {
    if (code <= 0 || code > 255) {
        return -1;
    }
    struct ImeOp op;
    op.kind = IME_SHORTCUT;
    op.composing = 0;
    op.count = code;
    op.bytes = NULL;
    op.n = 0;
    return push_op(op);
}

int ime_take(struct ImeOp *out) {
    if (out == NULL) {
        return -1;
    }
    pthread_mutex_lock(&g_mu);
    int rc = -1;
    if (g_count > 0) {
        *out = g_ops[g_head];
        g_ops[g_head].bytes = NULL;
        g_head = (g_head + 1) % IME_CAP;
        g_count--;
        rc = 0;
    }
    pthread_mutex_unlock(&g_mu);
    return rc;
}
