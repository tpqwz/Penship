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
    doc = doc_new();
    expect(doc_insert(doc, text, strlen(text)) == 0, "insert a table again");
    doc_place(doc, 2);
    expect(doc_insert(doc, "\n", 1) == 0, "enter leaves the row intact");
    expect(strcmp(doc_text(doc, NULL), text) == 0, "enter does not split the table");
    expect(doc_caret(doc) == 26, "enter moves down the same column");
    doc_place(doc, 30);
    expect(doc_insert(doc, "\n", 1) == 0, "enter on the last row adds one");
    expect(strstr(doc_text(doc, NULL), "| c | d |\n| | |") != NULL, "the new row keeps both columns");
    doc_free(doc);
    doc = doc_new();
    expect(doc_insert(doc, text, strlen(text)) == 0, "insert a table for a column");
    doc_place(doc, 2);
    expect(doc_table_column(doc, 1) == 0, "the table mark adds a column");
    expect(strstr(doc_text(doc, NULL), "| A | | B |") != NULL, "the new column follows A");
    expect(strstr(doc_text(doc, NULL), "| --- | --- | --- |") != NULL, "the delimiter gains a column");
    doc_free(doc);
    doc = doc_new();
    expect(doc_insert(doc, text, strlen(text)) == 0, "insert a table to drop a column");
    doc_place(doc, 6);
    expect(doc_table_column(doc, -1) == 0, "an extra column can be removed");
    expect(strstr(doc_text(doc, NULL), "| A |\n") != NULL, "B is gone");
    expect(doc_table_column(doc, -1) == -1, "the last column stays");
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

static void test_save_tail(void) {
    const char *path = "build/note-tail.md";
    Doc *doc = doc_new();
    char buf[32];
    FILE *file = NULL;
    doc_insert(doc, "hello", 5);
    expect(doc_save(doc, path) == 0, "tail save");
    expect(doc_save(doc, path) == 0, "a clean save writes nothing new");
    doc_insert(doc, "!", 1);
    expect(doc_save(doc, path) == 0, "append save");
    file = fopen(path, "rb");
    expect(file != NULL, "tail file");
    if (file != NULL) {
        size_t n = fread(buf, 1, sizeof buf - 1, file);
        fclose(file);
        buf[n] = '\0';
        expect(strcmp(buf, "hello!") == 0, "the append is on disk");
    }
    expect(doc_backspace(doc) == 0, "delete the appended mark");
    expect(doc_save(doc, path) == 0, "shorten save");
    file = fopen(path, "rb");
    if (file != NULL) {
        size_t n = fread(buf, 1, sizeof buf - 1, file);
        fclose(file);
        buf[n] = '\0';
        expect(strcmp(buf, "hello") == 0, "the end was cut");
    }
    doc_move(doc, -5);
    expect(doc_insert(doc, "X", 1) == 0, "insert in the middle");
    expect(doc_save(doc, path) == 0, "middle save");
    file = fopen(path, "rb");
    if (file != NULL) {
        size_t n = fread(buf, 1, sizeof buf - 1, file);
        fclose(file);
        buf[n] = '\0';
        expect(strcmp(buf, "Xhello") == 0, "a middle edit replaces the file");
    }
    doc_free(doc);
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

static void test_mark(void) {
    Doc *doc = doc_new();
    expect(doc != NULL, "mark doc");
    if (doc == NULL) {
        return;
    }
    expect(doc_mark(NULL, "**", 2) == -1, "a null document is refused");
    expect(doc_mark(doc, NULL, 2) == -1, "a null mark is refused");
    expect(doc_mark(doc, "**\n", 3) == -1, "a newline mark is refused");
    expect(doc_insert(doc, "hi", 2) == 0, "seed hi");
    doc_place(doc, 0);
    doc_seek(doc, 2);
    expect(doc_mark(doc, "**", 2) == 0, "bold wraps the selection");
    expect(strcmp(doc_text(doc, NULL), "**hi**") == 0, "bold marks surround hi");
    doc_place(doc, 2);
    doc_seek(doc, 4);
    expect(doc_mark(doc, "**", 2) == 0, "bold toggles off around the word");
    expect(strcmp(doc_text(doc, NULL), "hi") == 0, "the bold marks are gone");
    expect(doc_undo(doc) == 0 && strcmp(doc_text(doc, NULL), "**hi**") == 0, "undo restores the wrap");
    doc_load_bytes(doc, "**hi**", 6);
    doc_place(doc, 2);
    doc_seek(doc, 4);
    expect(doc_mark(doc, "*", 1) == 0, "italic does not strip a bold wrap");
    expect(strcmp(doc_text(doc, NULL), "***hi***") == 0, "italic adds a star around bold");
    doc_load_bytes(doc, "say `hi` please", 15);
    doc_place(doc, 5);
    doc_seek(doc, 7);
    expect(doc_mark(doc, "`", 1) == 0, "code toggles off");
    expect(strcmp(doc_text(doc, NULL), "say hi please") == 0, "the code ticks are gone");
    doc_load_bytes(doc, "go", 2);
    doc_place(doc, 2);
    expect(doc_mark(doc, "~~", 2) == 0, "an empty selection inserts strike marks");
    expect(strcmp(doc_text(doc, NULL), "go~~~~") == 0 && doc_caret(doc) == 4, "the caret sits between the marks");
    expect(doc_load_bytes(doc, "say hello now", 14) == 0, "seed a sentence");
    doc_place(doc, 6);
    expect(doc_select_word(doc) == 0, "the caret takes the word");
    expect(doc_anchor(doc) == 4 && doc_caret(doc) == 9, "hello is the selected word");
    expect(doc_mark(doc, "**", 2) == 0, "bold wraps that word");
    expect(strcmp(doc_text(doc, NULL), "say **hello** now") == 0, "bold does not split the word");
    doc_free(doc);
}

static void test_bar_actions(void) {
    Doc *doc = doc_new();
    expect(doc != NULL, "bar doc");
    if (doc == NULL) {
        return;
    }
    expect(doc_insert(doc, "Hello", 5) == 0, "seed Hello");
    doc_place(doc, 0);
    expect(doc_line(doc, 1) == 0 && strcmp(doc_text(doc, NULL), "# Hello") == 0, "a heading mark");
    expect(doc_line(doc, 1) == 0 && strcmp(doc_text(doc, NULL), "## Hello") == 0, "the heading deepens");
    expect(doc_line(doc, 1) == 0 && strcmp(doc_text(doc, NULL), "### Hello") == 0, "the heading reaches three");
    expect(doc_line(doc, 1) == 0 && strcmp(doc_text(doc, NULL), "Hello") == 0, "the heading comes off");
    expect(doc_line(doc, 2) == 0 && strcmp(doc_text(doc, NULL), "- Hello") == 0, "a bullet");
    expect(doc_line(doc, 3) == 0 && strcmp(doc_text(doc, NULL), "- [ ] Hello") == 0, "a bullet becomes a task");
    expect(doc_line(doc, 3) == 0 && strcmp(doc_text(doc, NULL), "Hello") == 0, "the task comes off");
    expect(doc_line(doc, 4) == 0 && strcmp(doc_text(doc, NULL), "> Hello") == 0, "a quote");
    expect(doc_line(doc, 4) == 0 && strcmp(doc_text(doc, NULL), "Hello") == 0, "the quote comes off");
    doc_place(doc, 0);
    doc_seek(doc, 5);
    expect(doc_link(doc) == 0 && strcmp(doc_text(doc, NULL), "[Hello]()") == 0, "the selection becomes a link");
    expect(doc_caret(doc) == 8, "the caret waits in the url");
    doc_load_bytes(doc, "ab", 2);
    doc_place(doc, 1);
    expect(doc_block(doc, 1) == 0 && strcmp(doc_text(doc, NULL), "a\n---\nb") == 0, "a rule splits the line");
    doc_load_bytes(doc, "", 0);
    expect(doc_block(doc, 3) == 0 && strcmp(doc_text(doc, NULL), "```\n\n```\n") == 0, "a fenced block");
    expect(doc_caret(doc) == 4, "the caret sits inside the fence");
    doc_load_bytes(doc, "", 0);
    expect(doc_block(doc, 2) == 0 && doc_caret(doc) == 2, "a table places the caret in the first cell");
    expect(doc_line(NULL, 1) == -1 && doc_block(doc, 9) == -1, "a bad block is refused");
    const char *words = "hello world";
    size_t wlo = 0;
    size_t whi = 0;
    expect(doc_word_at(words, 11, 1, &wlo, &whi) == 0 && wlo == 0 && whi == 5, "hello is the word");
    expect(doc_word_at(words, 11, 5, &wlo, &whi) == 0 && wlo == 0 && whi == 5, "the gap after hello stays with hello");
    expect(doc_word_at(words, 11, 6, &wlo, &whi) == 0 && wlo == 6 && whi == 11, "world is the word");
    expect(doc_word_at("  ", 2, 0, &wlo, &whi) == -1, "spaces are not a word");
    expect(doc_word_at(NULL, 3, 0, &wlo, &whi) == -1, "a null buffer is not a word");
    doc_free(doc);
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
    test_save_tail();
    test_save_load();
    test_rope();
    test_mark();
    test_bar_actions();
    if (fail) {
        return 1;
    }
    puts("doc_test ok");
    return 0;
}
