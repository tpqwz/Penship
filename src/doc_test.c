#include "doc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail;

static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "fail: %s\n", msg);
        fail = 1;
    }
}

static void test_insert_and_move(void) {
    Doc *doc = doc_new();
    expect(doc != NULL, "new");
    size_t len = 99;
    expect(doc_text(doc, &len) != NULL && len == 0, "empty");
    expect(doc_caret(doc) == 0, "caret starts at 0");
    expect(doc_insert(doc, "hi", 2) == 0, "insert hi");
    expect(strcmp(doc_text(doc, &len), "hi") == 0 && len == 2, "text is hi");
    expect(doc_caret(doc) == 2, "caret after hi");

    doc_move(doc, -1);
    expect(doc_caret(doc) == 1, "move left");
    expect(doc_insert(doc, "X", 1) == 0, "insert X");
    expect(strcmp(doc_text(doc, &len), "hXi") == 0, "hXi");
    expect(doc_caret(doc) == 2, "caret after X");

    doc_move(doc, -10);
    expect(doc_caret(doc) == 0, "move clamps at the start");
    doc_move(doc, 10);
    expect(doc_caret(doc) == 3, "move clamps at the end");
    expect(doc_backspace(doc) == 0, "backspace i");
    expect(strcmp(doc_text(doc, &len), "hX") == 0, "hX");
    doc_move(doc, -2);
    expect(doc_backspace(doc) == -1, "backspace at start");
    expect(strcmp(doc_text(doc, &len), "hX") == 0, "unchanged at start");
    doc_free(doc);
}

static void test_utf8(void) {
    Doc *doc = doc_new();
    const char eacute[] = "\xc3\xa9";
    expect(doc_insert(doc, eacute, 2) == 0, "insert e acute");
    expect(doc_caret(doc) == 2, "caret after e acute");
    doc_move(doc, -1);
    expect(doc_caret(doc) == 0, "move left skips the whole code point");
    doc_move(doc, 1);
    expect(doc_backspace(doc) == 0, "backspace e acute");
    size_t len = 1;
    expect(doc_text(doc, &len)[0] == '\0' && len == 0, "empty after backspace");

    const char emoji[] = "\xf0\x9f\x91\x8d";
    expect(doc_insert(doc, emoji, 4) == 0, "insert emoji");
    doc_move(doc, -1);
    expect(doc_caret(doc) == 0, "move left across emoji");
    doc_move(doc, 1);
    expect(doc_backspace(doc) == 0 && doc_caret(doc) == 0, "backspace emoji");

    expect(doc_insert(doc, "a", 1) == 0, "insert a");
    expect(doc_insert(doc, "\xff", 1) == -1, "reject bare continuation");
    expect(doc_insert(doc, "\xc0\x80", 2) == -1, "reject overlong");
    expect(doc_insert(doc, "\xed\xa0\x80", 3) == -1, "reject surrogate");
    expect(strcmp(doc_text(doc, &len), "a") == 0 && doc_caret(doc) == 1, "rejects leave the doc");
    doc_free(doc);
}

static void test_delete_forward(void) {
    Doc *doc = doc_new();
    expect(doc_insert(doc, "ab", 2) == 0, "insert ab");
    doc_move(doc, -2);
    expect(doc_caret(doc) == 0, "caret at a");
    expect(doc_delete(doc) == 0, "delete a");
    expect(strcmp(doc_text(doc, NULL), "b") == 0 && doc_caret(doc) == 0, "b remains");
    expect(doc_delete(doc) == 0, "delete b");
    expect(strcmp(doc_text(doc, NULL), "") == 0 && doc_caret(doc) == 0, "empty");
    expect(doc_delete(doc) == -1, "delete at the end");

    const char emoji[] = "\xf0\x9f\x91\x8dX";
    expect(doc_insert(doc, emoji, 5) == 0, "insert emoji and X");
    doc_move(doc, -2);
    expect(doc_caret(doc) == 0, "caret before the emoji");
    expect(doc_delete(doc) == 0, "delete the emoji");
    expect(strcmp(doc_text(doc, NULL), "X") == 0 && doc_caret(doc) == 0, "X remains");
    expect(doc_undo(doc) == 0, "undo the forward delete");
    expect(strcmp(doc_text(doc, NULL), emoji) == 0 && doc_caret(doc) == 0, "emoji restored");

    doc_move(doc, 1);
    doc_extend(doc, 1);
    expect(doc_delete(doc) == 0, "delete the selection");
    expect(strcmp(doc_text(doc, NULL), "\xf0\x9f\x91\x8d") == 0 && doc_caret(doc) == 4, "X was selected");
    doc_free(doc);
}

static void test_place(void) {
    Doc *doc = doc_new();
    expect(doc_insert(doc, "hello", 5) == 0, "insert hello");
    doc_place(doc, 2);
    expect(doc_caret(doc) == 2 && doc_anchor(doc) == 2, "place between letters");
    doc_extend(doc, 2);
    expect(doc_anchor(doc) == 2 && doc_caret(doc) == 4, "selection is open");
    doc_place(doc, 0);
    expect(doc_caret(doc) == 0 && doc_anchor(doc) == 0, "place clears the selection");
    doc_place(doc, 100);
    expect(doc_caret(doc) == 5, "place clamps to the end");
    expect(doc_load_bytes(doc, "\xf0\x9f\x91\x8dZ", 5) == 0, "emoji then Z");
    doc_place(doc, 2);
    expect(doc_caret(doc) == 0, "place snaps to the start of the emoji");
    doc_place(doc, 4);
    doc_seek(doc, 0);
    expect(doc_anchor(doc) == 4 && doc_caret(doc) == 0, "seek keeps the anchor");
    doc_select_all(doc);
    expect(doc_anchor(doc) == 0 && doc_caret(doc) == 5, "select all");
    doc_free(doc);
}

static void test_task(void) {
    Doc *doc = doc_new();
    const char *text = "- [ ] milk\n- [x] eggs";
    expect(doc_insert(doc, text, strlen(text)) == 0, "insert tasks");
    expect(doc_toggle_task(doc, 3) == 0, "toggle the open box");
    expect(strcmp(doc_text(doc, NULL), "- [x] milk\n- [x] eggs") == 0, "the box is checked");
    expect(doc_toggle_task(doc, 14) == 0, "toggle the checked box");
    expect(strcmp(doc_text(doc, NULL), "- [x] milk\n- [ ] eggs") == 0, "the box is cleared");
    expect(doc_toggle_task(doc, 8) == -1, "the words are not the box");
    doc_free(doc);
}

static void test_table_tab(void) {
    Doc *doc = doc_new();
    const char *text = "| A | B |\n| --- | --- |\n| c | d |";
    expect(doc_insert(doc, text, strlen(text)) == 0, "insert a table");
    doc_place(doc, 2);
    expect(doc_table_tab(doc, 1) == 0 && doc_caret(doc) == 6, "tab moves from A to B");
    expect(doc_table_tab(doc, 1) == 0 && doc_caret(doc) == 26, "tab skips the delimiter onto c");
    expect(doc_table_tab(doc, 1) == 0 && doc_caret(doc) == 30, "tab moves from c to d");
    size_t before = 0;
    doc_text(doc, &before);
    expect(doc_table_tab(doc, 1) == 1, "tab off the last cell adds a row");
    size_t after = 0;
    const char *got = doc_text(doc, &after);
    expect(after > before && got[doc_caret(doc)] != '|', "the new row has a cell for the caret");
    expect(doc_table_tab(doc, -1) == 0 && doc_caret(doc) == 30, "shift-tab returns to d");
    doc_place(doc, 0);
    expect(doc_load_bytes(doc, "hello", 5) == 0, "leave the table");
    expect(doc_table_tab(doc, 1) == -1, "tab outside a table does nothing");
    doc_free(doc);
}

static void test_newline_continues_marker(void) {
    Doc *doc = doc_new();
    expect(doc_insert(doc, "- milk", 6) == 0, "a bullet");
    expect(doc_insert(doc, "\n", 1) == 0, "enter continues the bullet");
    expect(strcmp(doc_text(doc, NULL), "- milk\n- ") == 0 && doc_caret(doc) == 9, "the next bullet is ready");
    expect(doc_insert(doc, "\n", 1) == 0, "enter on an empty bullet leaves the list");
    expect(strcmp(doc_text(doc, NULL), "- milk\n") == 0 && doc_caret(doc) == 7, "the empty marker is gone");
    expect(doc_undo(doc) == 0, "undo leaving the list");
    expect(strcmp(doc_text(doc, NULL), "- milk\n- ") == 0, "the bullet marker returns");

    expect(doc_load_bytes(doc, "1. figs", 7) == 0, "an ordered item");
    doc_place(doc, 7);
    expect(doc_insert(doc, "\n", 1) == 0, "enter continues the number");
    expect(strcmp(doc_text(doc, NULL), "1. figs\n2. ") == 0, "the next number is one higher");

    expect(doc_load_bytes(doc, "- [x] eggs", 10) == 0, "a checked task");
    doc_place(doc, 10);
    expect(doc_insert(doc, "\n", 1) == 0, "enter continues the task");
    expect(strcmp(doc_text(doc, NULL), "- [x] eggs\n- [ ] ") == 0, "the new box is open");

    expect(doc_load_bytes(doc, "> note", 6) == 0, "a quote");
    doc_place(doc, 6);
    expect(doc_insert(doc, "\n", 1) == 0, "enter continues the quote");
    expect(strcmp(doc_text(doc, NULL), "> note\n> ") == 0, "the quote mark continues");

    expect(doc_load_bytes(doc, "    - nest", 10) == 0, "a nested bullet");
    doc_place(doc, 10);
    expect(doc_insert(doc, "\n", 1) == 0, "enter keeps the indent");
    expect(strcmp(doc_text(doc, NULL), "    - nest\n    - ") == 0, "four spaces stay");

    expect(doc_load_bytes(doc, "> - milk", 8) == 0, "a quote and a bullet");
    doc_place(doc, 8);
    expect(doc_insert(doc, "\n", 1) == 0, "enter continues both marks");
    expect(strcmp(doc_text(doc, NULL), "> - milk\n> - ") == 0, "quote and bullet both stay");

    expect(doc_load_bytes(doc, "- helXlo", 8) == 0, "a bullet to split");
    doc_place(doc, 5);
    expect(doc_insert(doc, "\n", 1) == 0, "enter in the words splits the item");
    expect(strcmp(doc_text(doc, NULL), "- hel\n- Xlo") == 0, "each piece keeps a bullet");

    expect(doc_load_bytes(doc, "- milk", 6) == 0, "a bullet whose marker is edited");
    doc_place(doc, 1);
    expect(doc_insert(doc, "\n", 1) == 0, "enter inside the marker is a plain newline");
    expect(strcmp(doc_text(doc, NULL), "-\n milk") == 0, "the marker was not repeated");

    expect(doc_load_bytes(doc, "plain", 5) == 0, "prose");
    doc_place(doc, 5);
    expect(doc_insert(doc, "\n", 1) == 0, "enter on prose is a newline");
    expect(strcmp(doc_text(doc, NULL), "plain\n") == 0 && doc_caret(doc) == 6, "no marker was invented");

    expect(doc_load_bytes(doc, "- abcd", 6) == 0, "a bullet with a selection");
    doc_place(doc, 4);
    doc_extend(doc, 2);
    expect(doc_insert(doc, "\n", 1) == 0, "enter replaces the selection");
    expect(strcmp(doc_text(doc, NULL), "- ab\n") == 0, "a selection does not continue the marker");
    doc_free(doc);
}

static void test_rows(void) {
    Doc *doc = doc_new();
    expect(doc_insert(doc, "ab\ncd", 5) == 0, "two lines");
    expect(doc_caret(doc) == 5, "caret at end");
    doc_move_row(doc, -1);
    expect(doc_caret(doc) == 2, "column 2 of the first line");
    doc_move_row(doc, -1);
    expect(doc_caret(doc) == 0, "no previous line");
    doc_move_row(doc, 1);
    expect(doc_caret(doc) == 3, "column 0 of the second line");
    doc_move(doc, 1);
    doc_move_row(doc, 1);
    expect(doc_caret(doc) == 5, "no next line stays at the end");

    doc_free(doc);
    doc = doc_new();
    doc_insert(doc, "ab\nc", 4);
    doc_move(doc, -3);
    expect(doc_caret(doc) == 1, "on b");
    doc_move_row(doc, 1);
    expect(doc_caret(doc) == 4, "column clamped to the short line");
    doc_free(doc);
}

static void test_load_bytes(void) {
    Doc *doc = doc_new();
    expect(doc_insert(doc, "keep", 4) == 0, "seed");
    expect(doc_load_bytes(doc, "opened\n", 7) == 0, "load bytes");
    size_t len = 0;
    expect(strcmp(doc_text(doc, &len), "opened\n") == 0 && len == 7, "bytes replace the doc");
    expect(doc_caret(doc) == 7, "caret at end of loaded bytes");

    expect(doc_load_bytes(doc, "", 0) == 0, "empty file");
    expect(doc_text(doc, &len)[0] == '\0' && len == 0 && doc_caret(doc) == 0, "empty bytes");
    expect(doc_load_bytes(doc, NULL, 0) == 0, "null empty is an empty file");

    expect(doc_load_bytes(doc, "back", 4) == 0, "restore");
    expect(doc_load_bytes(doc, "a\xff", 2) == -1, "reject invalid utf-8 bytes");
    expect(strcmp(doc_text(doc, &len), "back") == 0 && doc_caret(doc) == 4, "bad bytes keep the doc");
    expect(doc_load_bytes(doc, NULL, 3) == -1, "null with a length");
    expect(strcmp(doc_text(doc, &len), "back") == 0, "null length keeps the doc");
    /* The pointer is short on purpose: the cap must be checked before any read. */
    expect(doc_load_bytes(doc, "x", (size_t)DOC_MAX_BYTES + 1) == -1, "reject over 4 MiB");
    expect(strcmp(doc_text(doc, &len), "back") == 0, "oversize keeps the doc");
    doc_free(doc);
}

static void test_selection_and_undo(void) {
    Doc *doc = doc_new();
    expect(doc_insert(doc, "hello", 5) == 0, "insert hello");
    expect(doc_caret(doc) == 5 && doc_anchor(doc) == 5, "insert leaves no selection");
    doc_move(doc, -4);
    expect(doc_caret(doc) == 1 && doc_anchor(doc) == 1, "move collapses to the caret");
    doc_extend(doc, 3);
    expect(doc_caret(doc) == 4 && doc_anchor(doc) == 1, "extend keeps the anchor");
    expect(doc_insert(doc, "X", 1) == 0, "typing replaces the selection");
    expect(strcmp(doc_text(doc, NULL), "hXo") == 0, "ell became X");
    expect(doc_caret(doc) == 2 && doc_anchor(doc) == 2, "replace clears the selection");
    expect(doc_undo(doc) == 0, "undo the replacement");
    expect(strcmp(doc_text(doc, NULL), "hello") == 0, "hello restored");
    expect(doc_caret(doc) == 4 && doc_anchor(doc) == 1, "selection restored");
    expect(doc_undo(doc) == 0, "undo the original insert");
    expect(strcmp(doc_text(doc, NULL), "") == 0 && doc_caret(doc) == 0, "document empty again");
    expect(doc_undo(doc) == -1, "undo stops when the stack is empty");

    expect(doc_insert(doc, "ab", 2) == 0, "insert ab");
    doc_move(doc, -1);
    doc_extend(doc, 1);
    expect(doc_backspace(doc) == 0, "backspace deletes the selection");
    expect(strcmp(doc_text(doc, NULL), "a") == 0 && doc_caret(doc) == 1, "b was selected");
    expect(doc_undo(doc) == 0, "undo the selection delete");
    expect(strcmp(doc_text(doc, NULL), "ab") == 0 && doc_anchor(doc) == 1 && doc_caret(doc) == 2, "ab selected again");

    expect(doc_load_bytes(doc, "ab\ncd", 5) == 0, "two-line note");
    doc_move(doc, -5);
    doc_extend_row(doc, 1);
    expect(doc_caret(doc) == 3 && doc_anchor(doc) == 0, "extend selects through the newline");
    expect(doc_insert(doc, "Z", 1) == 0, "replace a row selection");
    expect(strcmp(doc_text(doc, NULL), "Zcd") == 0, "row selection replaced");
    doc_free(doc);

    Doc *many = doc_new();
    for (int i = 0; i < 33; i++) {
        expect(doc_insert(many, "z", 1) == 0, "insert z");
    }
    int undone = 0;
    while (doc_undo(many) == 0) {
        undone++;
    }
    size_t len = 0;
    expect(undone == 32, "undo keeps 32 edits");
    expect(doc_text(many, &len)[0] == 'z' && len == 1, "the oldest insert remains");
    expect(doc_load_bytes(many, "b", 1) == 0, "load replaces the note");
    expect(doc_undo(many) == -1, "load clears undo");
    expect(strcmp(doc_text(many, NULL), "b") == 0, "loaded b");
    doc_free(many);
}

static void test_save_load(void) {
    const char *path = "build/note-test.md";
    Doc *doc = doc_new();
    doc_insert(doc, "one\n", 4);
    expect(doc_save(doc, path) == 0, "save");
    doc_move(doc, -2);
    doc_insert(doc, "X", 1);
    expect(strcmp(doc_text(doc, NULL), "onXe\n") == 0, "edited after save");

    Doc *loaded = doc_new();
    expect(doc_load(loaded, path) == 0, "load");
    size_t len = 0;
    expect(strcmp(doc_text(loaded, &len), "one\n") == 0 && len == 4, "loaded bytes");
    expect(doc_caret(loaded) == 4, "caret at end of load");
    expect(doc_load(loaded, "build/missing-note.md") == -1, "missing file");
    expect(strcmp(doc_text(loaded, &len), "one\n") == 0, "missing file keeps the doc");

    expect(doc_insert(doc, "\xff", 1) == -1, "still valid");
    FILE *bad = fopen("build/bad-note.md", "wb");
    expect(bad != NULL, "bad file");
    if (bad) {
        fwrite("a\xff", 1, 2, bad);
        fclose(bad);
    }
    expect(doc_load(loaded, "build/bad-note.md") == -1, "invalid utf-8 file");
    expect(strcmp(doc_text(loaded, &len), "one\n") == 0, "bad file keeps the doc");
    doc_free(doc);
    doc_free(loaded);
    remove(path);
    remove("build/bad-note.md");
}

static void test_rope(void) {
    Doc *doc = doc_new();
    char *block = malloc(5000);
    expect(doc != NULL && block != NULL, "rope alloc");
    if (doc == NULL || block == NULL) {
        free(block);
        doc_free(doc);
        return;
    }
    memset(block, 'a', 5000);
    expect(doc_insert(doc, block, 5000) == 0, "a long run is stored");
    doc_place(doc, 100);
    expect(doc_insert(doc, "XYZ", 3) == 0, "insert inside the rope");
    size_t len = 0;
    const char *s = doc_text(doc, &len);
    expect(len == 5003 && s[100] == 'X' && s[101] == 'Y' && s[102] == 'Z' && s[103] == 'a', "the rope keeps the bytes");
    doc_place(doc, 4090);
    doc_seek(doc, 4110);
    expect(doc_insert(doc, "Q", 1) == 0, "replace across a leaf boundary");
    s = doc_text(doc, &len);
    expect(len == 5003 - 20 + 1 && s[4090] == 'Q', "a boundary edit stays in order");
    doc_free(doc);
    free(block);
}

int main(void) {
    test_insert_and_move();
    test_utf8();
    test_delete_forward();
    test_place();
    test_task();
    test_table_tab();
    test_newline_continues_marker();
    test_rows();
    test_load_bytes();
    test_selection_and_undo();
    test_save_load();
    test_rope();
    if (fail) {
        return 1;
    }
    puts("doc_test ok");
    return 0;
}
