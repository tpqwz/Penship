#include "ime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failed;

static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "fail: %s\n", msg);
        g_failed = 1;
    }
}

static void test_order(void) {
    ime_reset();
    expect(ime_push_text("Hi", 2, 0) == 0, "commit Hi");
    expect(ime_push_erase(1) == 0, "erase one");
    expect(ime_push_text("é", 2, 1) == 0, "compose e acute");
    expect(ime_push_finish() == 0, "finish");
    struct ImeOp op;
    expect(ime_take(&op) == 0 && op.kind == IME_TEXT && op.composing == 0 && op.n == 2 && memcmp(op.bytes, "Hi", 2) == 0,
           "first is Hi");
    free(op.bytes);
    expect(ime_take(&op) == 0 && op.kind == IME_ERASE && op.count == 1, "then erase");
    expect(ime_take(&op) == 0 && op.kind == IME_TEXT && op.composing == 1 && op.n == 2, "then composition");
    free(op.bytes);
    expect(ime_take(&op) == 0 && op.kind == IME_FINISH, "then finish");
    expect(ime_take(&op) == -1, "queue is empty");
}

static void test_limits(void) {
    ime_reset();
    expect(ime_push_text("\xff", 1, 0) == -1, "reject invalid utf-8");
    expect(ime_push_text(NULL, 3, 0) == -1, "reject a null commit");
    expect(ime_push_erase(0) == -1, "reject a zero erase");
    expect(ime_push_text("", 0, 0) == 0, "an empty commit is allowed");
    char big[5000];
    memset(big, 'a', sizeof big);
    expect(ime_push_text(big, sizeof big, 0) == -1, "reject a commit over 4096 bytes");
    ime_reset();
    int pushed = 0;
    for (int i = 0; i < 40; i++) {
        if (ime_push_erase(1) == 0) {
            pushed++;
        }
    }
    expect(pushed == 32, "the queue holds 32 ops");
    int took = 0;
    struct ImeOp op;
    while (ime_take(&op) == 0) {
        took++;
        free(op.bytes);
    }
    expect(took == 32, "32 ops come back");
    ime_reset();
    expect(ime_push_undo() == 0, "queue an undo");
    expect(ime_take(&op) == 0 && op.kind == IME_UNDO && op.bytes == NULL, "undo comes back");
    expect(ime_push_shortcut(44) == 0, "queue a shortcut");
    expect(ime_take(&op) == 0 && op.kind == IME_SHORTCUT && op.count == 44, "shortcut comes back");
    expect(ime_push_shortcut(0) == -1, "reject a zero shortcut");
    expect(ime_push_forward(1) == 0, "queue a forward delete");
    expect(ime_take(&op) == 0 && op.kind == IME_FORWARD && op.count == 1, "forward delete comes back");
    expect(ime_push_forward(0) == -1, "reject a zero forward delete");
    expect(ime_push_forward(257) == -1, "reject a huge forward delete");
    expect(ime_push_move(-1, 0) == 0, "queue a move left");
    expect(ime_take(&op) == 0 && op.kind == IME_MOVE && op.count == -1 && op.composing == 0, "move comes back");
    expect(ime_push_move(0, 0) == -1, "reject a zero move");
    expect(ime_push_row(1, 1) == 0, "queue a row extend");
    expect(ime_take(&op) == 0 && op.kind == IME_ROW && op.count == 1 && op.composing == 1, "row comes back");
    expect(ime_push_row(2, 0) == -1, "reject a row jump");
    expect(ime_push_clip(IME_COPY) == 0, "queue a copy");
    expect(ime_take(&op) == 0 && op.kind == IME_COPY && op.bytes == NULL, "copy comes back");
    expect(ime_push_clip(IME_PASTE) == 0, "queue a paste");
    expect(ime_take(&op) == 0 && op.kind == IME_PASTE, "paste comes back");
    expect(ime_push_clip(1) == -1, "reject a text op as a clip");
}

int main(void) {
    test_order();
    test_limits();
    if (g_failed) {
        return 1;
    }
    printf("ime_test ok\n");
    return 0;
}
