#include "page.h"

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

static unsigned char *load_font(int *len) {
    FILE *file = fopen("assets/LiberationSans-Regular.ttf", "rb");
    expect(file != NULL, "open font");
    if (file == NULL) {
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    unsigned char *bytes = malloc((size_t)size);
    expect(bytes != NULL && fread(bytes, 1, (size_t)size, file) == (size_t)size, "read font");
    fclose(file);
    *len = (int)size;
    return bytes;
}

static int ink_at(const uint8_t *px, int width, int x, int y) {
    const uint8_t *p = px + ((size_t)y * (size_t)width + (size_t)x) * 4;
    return p[0] != PAGE_PAPER_R || p[1] != PAGE_PAPER_G || p[2] != PAGE_PAPER_B;
}

static void test_caret_and_glyph(const unsigned char *font, int font_len) {
    expect(page_set_font(NULL, 0) == -1, "reject empty font");
    expect(page_set_font(font, font_len) == 0, "set font");

    const int width = 240;
    const int height = 160;
    const int font_px = 32;
    int margin = page_margin(font_px);
    uint8_t *px = calloc((size_t)width * (size_t)height, 4);
    expect(px != NULL, "alloc");
    if (px == NULL) {
        return;
    }

    page_draw(px, width, height, 0, 0, width, height, "", 0, 0, 0, font_px);
    expect(!ink_at(px, width, 0, 0), "corner stays paper");
    int caret_ink = 0;
    int caret_inside = 1;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            if (!ink_at(px, width, x, y)) {
                continue;
            }
            caret_ink++;
            if (x < margin || x >= margin + 4) {
                caret_inside = 0;
            }
        }
    }
    expect(caret_ink > 8, "empty page shows a caret");
    expect(caret_inside, "caret sits in the left margin column");
    int box_x = 0;
    int box_y = 0;
    int box_h = 0;
    expect(page_caret_box(&box_x, &box_y, &box_h) == 0, "the caret has a box");
    expect(box_h > 8 && box_x >= margin && box_x < margin + 4, "the caret box sits on the bar");

    page_draw(px, width, height, 0, 0, width, height, "H", 1, 1, 1, font_px);
    int letter = 0;
    for (int y = 0; y < height; y++) {
        for (int x = margin + 8; x < width; x++) {
            if (ink_at(px, width, x, y)) {
                letter = 1;
            }
        }
    }
    expect(letter, "H draws ink to the right of the margin");
    free(px);
}

static int near_rgb(const uint8_t *px, int width, int height, int r, int g, int b) {
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            const uint8_t *p = px + ((size_t)y * (size_t)width + (size_t)x) * 4;
            int dr = p[0] - r;
            int dg = p[1] - g;
            int db = p[2] - b;
            if (dr < 0) {
                dr = -dr;
            }
            if (dg < 0) {
                dg = -dg;
            }
            if (db < 0) {
                db = -db;
            }
            if (dr <= 12 && dg <= 12 && db <= 12) {
                return 1;
            }
        }
    }
    return 0;
}

static void test_hit(const unsigned char *font, int font_len) {
    expect(page_set_font(font, font_len) == 0, "hit font");
    const int width = 400;
    const int height = 300;
    const int font_px = 32;
    int margin = page_margin(font_px);
    const char *text = "ab\ncd";
    size_t at = page_hit(0, 0, width, height, text, 5, 5, font_px, margin, margin + 2);
    expect(at == 0, "the top left of the text is the start");
    at = page_hit(0, 0, width, height, text, 5, 5, font_px, width - 2, margin + 2);
    expect(at == 2, "the right side of the first row is the end of that line");

    uint8_t *px = calloc((size_t)width * (size_t)height, 4);
    expect(px != NULL, "hit buffer");
    if (px == NULL) {
        return;
    }
    page_draw(px, width, height, 0, 0, width, height, text, 5, 5, 5, font_px);
    int y = -1;
    int gap = 0;
    for (int row = margin + font_px; row < height; row++) {
        if (ink_at(px, width, margin + 12, row)) {
            if (gap > 4) {
                y = row + 2;
                break;
            }
        } else if (y < 0) {
            gap++;
        }
    }
    expect(y > margin, "the second line is on screen");
    at = page_hit(0, 0, width, height, text, 5, 5, font_px, margin + 4, y);
    expect(at == 3, "a tap on the second line lands on c");
    free(px);
}

static void test_table_cell(const unsigned char *font, int font_len) {
    expect(page_set_font(font, font_len) == 0, "table cell font");
    const int width = 420;
    const int height = 360;
    const int font_px = 32;
    int margin = page_margin(font_px);
    const char *text = "| Name | Qty |\n| --- | --- |\n| fig | 2 |\n";
    size_t n = strlen(text);
    size_t left = page_hit(0, 0, width, height, text, n, 0, font_px, margin + 8, margin + 8);
    size_t right = page_hit(0, 0, width, height, text, n, 0, font_px, width - margin - 8, margin + 8);
    expect(left >= 2 && left <= 6, "a tap on the left cell lands in Name");
    expect(right >= 9 && right <= 13, "a tap on the right cell lands in Qty");
}

static void test_link_url(void) {
    const char *text = "See [the note](https://penship.app) now\n";
    char url[64];
    expect(page_link_url(text, strlen(text), 6, url, sizeof url) == 0, "a link has an address");
    expect(strcmp(url, "https://penship.app") == 0, "the address is the https target");
    expect(page_link_url(text, strlen(text), 0, url, sizeof url) == -1, "the words before the link are not a link");
    expect(page_link_url("Go [x](file://etc) ", 18, 4, url, sizeof url) == -1, "a file address stays in the note");
}

static void test_word_wrap(const unsigned char *font, int font_len) {
    expect(page_set_font(font, font_len) == 0, "wrap font");
    const int width = 140;
    const int height = 400;
    const int font_px = 32;
    int margin = page_margin(font_px);
    const char *text = "Hello world";
    uint8_t *px = calloc((size_t)width * (size_t)height, 4);
    expect(px != NULL, "wrap buffer");
    if (px == NULL) {
        return;
    }
    page_set_editing(0);
    page_set_source(0);
    page_set_follow(1);
    page_draw(px, width, height, 0, 0, width, height, text, strlen(text), strlen(text), strlen(text), font_px);
    int y = -1;
    int runs = 0;
    int in_run = 0;
    for (int row = 0; row < height; row++) {
        int row_ink = 0;
        for (int col = 0; col < width; col++) {
            if (ink_at(px, width, col, row)) {
                row_ink = 1;
                break;
            }
        }
        if (row_ink && !in_run) {
            runs++;
            if (runs == 2) {
                y = row + 4;
            }
            in_run = 1;
        }
        if (!row_ink) {
            in_run = 0;
        }
    }
    expect(runs >= 2 && y > margin, "Hello world wraps onto a second line");
    size_t at = page_hit(0, 0, width, height, text, strlen(text), strlen(text), font_px, margin + 4, y);
    expect(at == 6, "the second line starts at world");
    free(px);
    const int tight = 100;
    uint8_t *tight_px = calloc((size_t)tight * (size_t)height, 4);
    expect(tight_px != NULL, "tight wrap buffer");
    if (tight_px == NULL) {
        return;
    }
    page_draw(tight_px, tight, height, 0, 0, tight, height, text, strlen(text), strlen(text), strlen(text), font_px);
    y = -1;
    runs = 0;
    in_run = 0;
    for (int row = 0; row < height; row++) {
        int row_ink = 0;
        for (int col = 0; col < tight; col++) {
            if (ink_at(tight_px, tight, col, row)) {
                row_ink = 1;
                break;
            }
        }
        if (row_ink && !in_run) {
            runs++;
            if (runs == 2) {
                y = row + 4;
            }
            in_run = 1;
        }
        if (!row_ink) {
            in_run = 0;
        }
    }
    expect(runs >= 2 && y > margin, "a word wider than the line still leaves the next word below");
    at = page_hit(0, 0, tight, height, text, strlen(text), strlen(text), font_px, margin + 4, y);
    expect(at >= 5, "the next line is not inside Hello");
    free(tight_px);
}

/* The stars are hidden on a rendered line, so a tap has to land on the letter. */
static void test_hit_rendered_mark(const unsigned char *font, int font_len) {
    expect(page_set_font(font, font_len) == 0, "rendered hit font");
    page_set_readonly(0);
    page_set_source(0);
    page_set_typewriter(0);
    page_set_follow(0);
    const int width = 480;
    const int height = 320;
    const int font_px = 32;
    int margin = page_margin(font_px);
    const char *text = "plain\n**Hi**\n";
    size_t n = strlen(text);
    uint8_t *px = calloc((size_t)width * (size_t)height, 4);
    expect(px != NULL, "rendered hit buffer");
    if (px == NULL) {
        return;
    }
    page_draw(px, width, height, 0, 0, width, height, text, n, 0, 0, font_px);
    int y = -1;
    int gap = 0;
    for (int row = margin + font_px; row < height; row++) {
        if (ink_at(px, width, margin + 4, row)) {
            if (gap > 4) {
                y = row + 2;
                break;
            }
        } else if (y < 0) {
            gap++;
        }
    }
    expect(y > margin, "the strong word is on the next line");
    size_t at = page_hit(0, 0, width, height, text, n, 0, font_px, margin + 4, y);
    expect(at == 8, "a tap on a rendered strong word hits the letter");
    page_set_line(1);
    page_draw(px, width, height, 0, 0, width, height, "Hello", 5, 0, 0, font_px);
    const uint8_t *band = px + ((size_t)(margin + 4) * (size_t)width + 2u) * 4;
    expect(band[0] < PAGE_PAPER_R && band[0] >= band[2], "the caret line is a warm band, not a selection");
    page_set_line(0);
    free(px);
}

static int ink_rows(const uint8_t *px, int width, int y0, int y1) {
    int n = 0;
    for (int y = y0; y < y1; y++) {
        for (int x = 0; x < width; x++) {
            if (ink_at(px, width, x, y)) {
                n++;
            }
        }
    }
    return n;
}

static void test_user_scroll(const unsigned char *font, int font_len) {
    expect(page_set_font(font, font_len) == 0, "scroll font");
    page_set_typewriter(0);
    page_set_follow(1);
    const int width = 200;
    const int height = 180;
    const int font_px = 32;
    int margin = page_margin(font_px);
    char text[80];
    text[0] = 'T';
    memset(text + 1, '\n', sizeof text - 1);
    uint8_t *px = calloc((size_t)width * (size_t)height, 4);
    expect(px != NULL, "scroll buffer");
    if (px == NULL) {
        return;
    }
    page_draw(px, width, height, 0, 0, width, height, text, sizeof text, sizeof text, sizeof text, font_px);
    int away = ink_rows(px, width, 0, margin + 4);
    expect(away == 0, "following the caret hides the top line");
    page_scroll_by(-1000000);
    page_draw(px, width, height, 0, 0, width, height, text, sizeof text, sizeof text, sizeof text, font_px);
    int back = ink_rows(px, width, margin, margin + font_px);
    expect(back > 8, "scrolling toward the start shows T");
    const uint8_t *bar = px + ((size_t)(margin + 8) * (size_t)width + (size_t)(width - 6)) * 4;
    expect(bar[0] != PAGE_PAPER_R || bar[1] != PAGE_PAPER_G || bar[2] != PAGE_PAPER_B,
           "a long note shows a scrollbar");
    page_draw(px, width, height, 0, 0, width, height, text, sizeof text, sizeof text, sizeof text, font_px);
    expect(ink_rows(px, width, margin, margin + font_px) > 8, "moving the caret without follow stays on the page");
    page_set_follow(1);
    page_draw(px, width, height, 0, 0, width, height, text, sizeof text, sizeof text, sizeof text, font_px);
    expect(ink_rows(px, width, 0, margin + 4) == 0, "follow brings the caret back");
    page_draw(px, width, height, 0, 0, width, height, "T", 1, 0, 0, font_px);
    bar = px + ((size_t)(margin + 8) * (size_t)width + (size_t)(width - 6)) * 4;
    expect(bar[0] == PAGE_PAPER_R && bar[1] == PAGE_PAPER_G && bar[2] == PAGE_PAPER_B,
           "a short note has no scrollbar");
    expect(page_caret_near(margin, margin + 4, 64), "the caret sits by the margin");
    expect(!page_caret_near(width - 2, height - 2, 24), "the far corner is not the caret");
    const char *many = "alpha\nbeta\ngamma\ndelta\n";
    int before = page_measures();
    page_draw(px, width, height, 0, 0, width, height, many, strlen(many), 0, 0, font_px);
    int measured = page_measures() - before;
    page_draw(px, width, height, 0, 0, width, height, many, strlen(many), 0, 0, font_px);
    expect(measured > 0 && page_measures() == before + measured, "a redraw reuses the line index");
    before = page_measures();
    page_draw(px, width, height, 0, 0, width, height, many, strlen(many), 6, 6, font_px);
    int moved = page_measures() - before;
    expect(moved == 1, "moving the caret measures only that line");
    expect(page_drag_kind(0, 0, 0, 0) == 0, "a press is still deciding");
    expect(page_drag_kind(1, 0, 0, 0) == 1, "a quick drag scrolls");
    expect(page_drag_kind(1, 0, 1, 0) == 2, "a drag from the caret selects");
    expect(page_drag_kind(1, 0, 0, 1) == 1, "typewriter still scrolls on a quick drag");
    expect(page_drag_kind(0, 1, 0, 0) == 2, "a held press selects");
    expect(page_drag_kind(1, 1, 0, 1) == 2, "a held press selects in typewriter too");
    expect(page_edge(100, 100, 500, 40) == -1, "the top zone scrolls up");
    expect(page_edge(200, 100, 500, 40) == 0, "the middle does not scroll");
    expect(page_edge(470, 100, 500, 40) == 1, "the bottom zone scrolls down");
    free(px);
}

static void test_markdown(const unsigned char *font, int font_len) {
    expect(page_set_font(font, font_len) == 0, "markdown font");
    const int width = 480;
    const int height = 240;
    const int font_px = 32;
    uint8_t *px = calloc((size_t)width * (size_t)height, 4);
    expect(px != NULL, "markdown alloc");
    if (px == NULL) {
        return;
    }
    const char *samples[] = {"# H", "*H*", "`H`", "[H](u)", "```\nH\n```", "- H", "> H", "1. H", "~~H~~"};
    const int reds[] = {PAGE_HEAD_R, PAGE_EM_R, PAGE_CODE_R, PAGE_LINK_R, PAGE_CODE_R, PAGE_LIST_R, PAGE_QUOTE_R,
                        PAGE_LIST_R, PAGE_STRIKE_R};
    const int greens[] = {PAGE_HEAD_G, PAGE_EM_G, PAGE_CODE_G, PAGE_LINK_G, PAGE_CODE_G, PAGE_LIST_G, PAGE_QUOTE_G,
                          PAGE_LIST_G, PAGE_STRIKE_G};
    const int blues[] = {PAGE_HEAD_B, PAGE_EM_B, PAGE_CODE_B, PAGE_LINK_B, PAGE_CODE_B, PAGE_LIST_B, PAGE_QUOTE_B,
                         PAGE_LIST_B, PAGE_STRIKE_B};
    const char *names[] = {"heading", "emphasis", "code", "link", "fenced code", "list", "quote", "ordered list",
                           "strike"};
    for (int i = 0; i < 9; i++) {
        page_draw(px, width, height, 0, 0, width, height, samples[i], strlen(samples[i]), strlen(samples[i]),
                  strlen(samples[i]), font_px);
        expect(near_rgb(px, width, height, reds[i], greens[i], blues[i]), names[i]);
    }
    page_draw(px, width, height, 0, 0, width, height, "```\nH\n```", 9, 9, 9, font_px);
    expect(near_rgb(px, width, height, PAGE_BLOCK_R, PAGE_BLOCK_G, PAGE_BLOCK_B), "fenced block wash");
    page_draw(px, width, height, 0, 0, width, height, "H", 1, 1, 1, font_px);
    expect(!near_rgb(px, width, height, PAGE_HEAD_R, PAGE_HEAD_G, PAGE_HEAD_B), "body is not a heading");
    free(px);
}

static void test_tall_note(const unsigned char *font, int font_len) {
    expect(page_set_font(font, font_len) == 0, "tall font");
    const int width = 80;
    const int height = 120;
    const size_t n = 600000;
    char *text = malloc(n);
    expect(text != NULL, "tall alloc");
    if (text == NULL) {
        return;
    }
    memset(text, '\n', n);
    uint8_t *px = calloc((size_t)width * (size_t)height, 4);
    expect(px != NULL, "tall page");
    if (px != NULL) {
        page_draw(px, width, height, 0, 0, width, height, text, n, n, n, 4096);
        int ink = 0;
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                if (ink_at(px, width, x, y)) {
                    ink++;
                }
            }
        }
        expect(ink > 8, "caret stays on screen past a 32-bit pixel height");
        free(px);
    }
    free(text);
}

static int count_near(const uint8_t *px, int width, int height, int r, int g, int b) {
    int n = 0;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            const uint8_t *p = px + ((size_t)y * (size_t)width + (size_t)x) * 4;
            int dr = p[0] - r;
            int dg = p[1] - g;
            int db = p[2] - b;
            if (dr < 0) {
                dr = -dr;
            }
            if (dg < 0) {
                dg = -dg;
            }
            if (db < 0) {
                db = -db;
            }
            if (dr <= 12 && dg <= 12 && db <= 12) {
                n++;
            }
        }
    }
    return n;
}

static int count_exact(const uint8_t *px, int width, int height, int r, int g, int b) {
    int n = 0;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            const uint8_t *p = px + ((size_t)y * (size_t)width + (size_t)x) * 4;
            if (p[0] == r && p[1] == g && p[2] == b) {
                n++;
            }
        }
    }
    return n;
}

static void test_rendered(const unsigned char *font, int font_len) {
    expect(page_set_font(font, font_len) == 0, "rendered font");
    const int width = 480;
    const int height = 320;
    const int font_px = 32;
    uint8_t *px = calloc((size_t)width * (size_t)height, 4);
    expect(px != NULL, "rendered alloc");
    if (px == NULL) {
        return;
    }
    const char *head = "### H\nZ";
    page_set_editing(0);
    page_draw(px, width, height, 0, 0, width, height, head, strlen(head), 0, 0, font_px);
    int head_closed = count_near(px, width, height, PAGE_HEAD_R, PAGE_HEAD_G, PAGE_HEAD_B);
    page_set_editing(1);
    page_draw(px, width, height, 0, 0, width, height, head, strlen(head), 0, 0, font_px);
    int head_source = count_near(px, width, height, PAGE_HEAD_R, PAGE_HEAD_G, PAGE_HEAD_B);
    expect(head_source > head_closed, "edit mode shows the marks on that line");
    page_set_editing(0);
    page_draw(px, width, height, 0, 0, width, height, head, strlen(head), strlen(head), strlen(head), font_px);
    int head_rendered = count_near(px, width, height, PAGE_HEAD_R, PAGE_HEAD_G, PAGE_HEAD_B);
    expect(head_rendered > 0 && head_rendered < head_source, "rendered heading hides the hashes");

    const char *h1 = "# Hi\nZ";
    const char *h2 = "## Hi\nZ";
    page_draw(px, width, height, 0, 0, width, height, h1, strlen(h1), strlen(h1), strlen(h1), font_px);
    int h1_lo = height;
    int h1_hi = 0;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            const uint8_t *p = px + ((size_t)y * (size_t)width + (size_t)x) * 4;
            int dr = (int)p[0] - PAGE_HEAD_R;
            int dg = (int)p[1] - PAGE_HEAD_G;
            int db = (int)p[2] - PAGE_HEAD_B;
            if (dr < 0) {
                dr = -dr;
            }
            if (dg < 0) {
                dg = -dg;
            }
            if (db < 0) {
                db = -db;
            }
            if (dr <= 12 && dg <= 12 && db <= 12) {
                if (y < h1_lo) {
                    h1_lo = y;
                }
                if (y > h1_hi) {
                    h1_hi = y;
                }
            }
        }
    }
    page_draw(px, width, height, 0, 0, width, height, h2, strlen(h2), strlen(h2), strlen(h2), font_px);
    int h2_lo = height;
    int h2_hi = 0;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            const uint8_t *p = px + ((size_t)y * (size_t)width + (size_t)x) * 4;
            int dr = (int)p[0] - PAGE_HEAD_R;
            int dg = (int)p[1] - PAGE_HEAD_G;
            int db = (int)p[2] - PAGE_HEAD_B;
            if (dr < 0) {
                dr = -dr;
            }
            if (dg < 0) {
                dg = -dg;
            }
            if (db < 0) {
                db = -db;
            }
            if (dr <= 12 && dg <= 12 && db <= 12) {
                if (y < h2_lo) {
                    h2_lo = y;
                }
                if (y > h2_hi) {
                    h2_hi = y;
                }
            }
        }
    }
    expect(h1_hi > h1_lo && h2_hi > h2_lo && h1_hi - h1_lo > h2_hi - h2_lo, "a single hash is taller than two");

    const char *list = "- H\nZ";
    page_set_editing(1);
    page_draw(px, width, height, 0, 0, width, height, list, strlen(list), 0, 0, font_px);
    int list_source = count_exact(px, width, height, PAGE_LIST_R, PAGE_LIST_G, PAGE_LIST_B);
    page_draw(px, width, height, 0, 0, width, height, list, strlen(list), strlen(list), strlen(list), font_px);
    page_set_editing(0);
    int list_rendered = count_exact(px, width, height, PAGE_LIST_R, PAGE_LIST_G, PAGE_LIST_B);
    expect(list_rendered > list_source && list_rendered > 16, "rendered list draws a bullet");

    const char *quote = "> H\nZ";
    page_set_editing(1);
    page_draw(px, width, height, 0, 0, width, height, quote, strlen(quote), 0, 0, font_px);
    int quote_source = count_exact(px, width, height, PAGE_QUOTE_R, PAGE_QUOTE_G, PAGE_QUOTE_B);
    page_draw(px, width, height, 0, 0, width, height, quote, strlen(quote), strlen(quote), strlen(quote), font_px);
    page_set_editing(0);
    int quote_rendered = count_exact(px, width, height, PAGE_QUOTE_R, PAGE_QUOTE_G, PAGE_QUOTE_B);
    expect(quote_rendered > quote_source && quote_rendered > 24, "rendered quote draws a bar");

    const char *fence = "```\na\n```\nZ";
    page_set_editing(1);
    page_draw(px, width, height, 0, 0, width, height, fence, strlen(fence), 0, 0, font_px);
    int fence_open = count_near(px, width, height, PAGE_CODE_R, PAGE_CODE_G, PAGE_CODE_B);
    page_draw(px, width, height, 0, 0, width, height, fence, strlen(fence), 4, 4, font_px);
    int fence_line = count_near(px, width, height, PAGE_CODE_R, PAGE_CODE_G, PAGE_CODE_B);
    page_draw(px, width, height, 0, 0, width, height, fence, strlen(fence), strlen(fence), strlen(fence), font_px);
    page_set_editing(0);
    int fence_rendered = count_near(px, width, height, PAGE_CODE_R, PAGE_CODE_G, PAGE_CODE_B);
    expect(fence_open > fence_rendered, "the opening line shows the fence ticks");
    expect(fence_line > fence_rendered, "an inner line keeps the whole code block open");
    expect(fence_rendered > 0 && fence_rendered < fence_open, "a caret outside the fence hides the ticks");
    expect(page_same_unit(fence, strlen(fence), 0, 4) == 1, "a fence is one segment");
    expect(page_same_unit(head, strlen(head), 0, strlen(head)) == 0, "two lines are two segments");
    expect(near_rgb(px, width, height, PAGE_BLOCK_R, PAGE_BLOCK_G, PAGE_BLOCK_B), "rendered fence keeps the wash");

    const char *rule = "---\nZ";
    page_draw(px, width, height, 0, 0, width, height, rule, strlen(rule), strlen(rule), strlen(rule), font_px);
    expect(near_rgb(px, width, height, PAGE_RULE_R, PAGE_RULE_G, PAGE_RULE_B), "a rule draws a bar");

    const char *task = "- [x] H\nZ";
    page_draw(px, width, height, 0, 0, width, height, task, strlen(task), strlen(task), strlen(task), font_px);
    expect(count_exact(px, width, height, PAGE_LIST_R, PAGE_LIST_G, PAGE_LIST_B) > 16, "a task draws a box");

    const char *setext = "Hello\n===\nZ";
    page_set_source(0);
    page_draw(px, width, height, 0, 0, width, height, setext, strlen(setext), strlen(setext), strlen(setext), font_px);
    int setext_rendered = count_near(px, width, height, PAGE_HEAD_R, PAGE_HEAD_G, PAGE_HEAD_B);
    page_set_source(1);
    page_draw(px, width, height, 0, 0, width, height, setext, strlen(setext), strlen(setext), strlen(setext), font_px);
    int setext_source = count_near(px, width, height, PAGE_HEAD_R, PAGE_HEAD_G, PAGE_HEAD_B);
    expect(setext_rendered > 0 && setext_source > 0, "a setext title is a heading, and source mode still paints it");
    page_set_source(0);

    const char *hashes = "### H\nZ";
    page_draw(px, width, height, 0, 0, width, height, hashes, strlen(hashes), strlen(hashes), strlen(hashes), font_px);
    int hybrid = count_near(px, width, height, PAGE_HEAD_R, PAGE_HEAD_G, PAGE_HEAD_B);
    page_set_source(1);
    page_draw(px, width, height, 0, 0, width, height, hashes, strlen(hashes), strlen(hashes), strlen(hashes), font_px);
    int sourced = count_near(px, width, height, PAGE_HEAD_R, PAGE_HEAD_G, PAGE_HEAD_B);
    expect(sourced > hybrid, "source mode shows the heading marks");
    page_set_source(0);

    const char *bare = "| A | B |\nZ";
    page_draw(px, width, height, 0, 0, width, height, bare, strlen(bare), strlen(bare), strlen(bare), font_px);
    expect(count_exact(px, width, height, PAGE_RULE_R, PAGE_RULE_G, PAGE_RULE_B) == 0, "a pipe row without a delimiter is not a table");

    const char *table = "| Name | Qty |\n| --- | ---: |\n| fig | 2 |\nZ";
    page_draw(px, width, height, 0, 0, width, height, table, strlen(table), strlen(table), strlen(table), font_px);
    int table_grid = count_exact(px, width, height, PAGE_RULE_R, PAGE_RULE_G, PAGE_RULE_B);
    int table_head = count_near(px, width, height, PAGE_HEAD_R, PAGE_HEAD_G, PAGE_HEAD_B);
    page_set_editing(1);
    page_draw(px, width, height, 0, 0, width, height, table, strlen(table), 0, 0, font_px);
    int table_source = count_exact(px, width, height, PAGE_RULE_R, PAGE_RULE_G, PAGE_RULE_B);
    page_set_editing(0);
    expect(table_grid > 40 && table_grid > table_source, "a rendered table draws a grid");
    expect(table_head > 0, "a rendered table header is a heading");
    const char *wide_table =
        "| Heading | a long phrase that wraps badly | ok |\n| --- | :---: | ---: |\n| fig | stays put | 2 |\n";
    const int narrow = 280;
    uint8_t *wide_px = calloc((size_t)narrow * 500, 4);
    expect(wide_px != NULL, "wide table buffer");
    if (wide_px != NULL) {
        page_set_editing(0);
        page_draw(wide_px, narrow, 500, 0, 0, narrow, 500, wide_table, strlen(wide_table), strlen(wide_table),
                  strlen(wide_table), 28);
        int head_y0 = -1;
        int head_y1 = -1;
        int ink_y0 = -1;
        for (int y = 0; y < 500; y++) {
            int head = 0;
            int ink = 0;
            for (int x = 0; x < narrow; x++) {
                const uint8_t *p = wide_px + ((size_t)y * (size_t)narrow + (size_t)x) * 4;
                if (p[0] > PAGE_HEAD_R - 12 && p[0] < PAGE_HEAD_R + 12 && p[1] > PAGE_HEAD_G - 12 &&
                    p[1] < PAGE_HEAD_G + 12 && p[2] > PAGE_HEAD_B - 12 && p[2] < PAGE_HEAD_B + 12) {
                    head = 1;
                }
                if (p[0] < 80 && p[1] < 80 && p[2] < 80) {
                    ink = 1;
                }
            }
            if (head) {
                if (head_y0 < 0) {
                    head_y0 = y;
                }
                head_y1 = y;
            }
            if (ink && head_y1 >= 0 && y > head_y1 + 4 && ink_y0 < 0) {
                ink_y0 = y;
            }
        }
        expect(head_y0 >= 0 && head_y1 > head_y0, "a squeezed table header is painted");
        expect(head_y1 - head_y0 > 36, "a long header cell wraps inside the row");
        expect(ink_y0 > head_y1, "body text starts after the header");
        expect(ink_y0 - head_y1 < 80, "the delimiter row is not a blank line");
        free(wide_px);
    }
    const char *one_word = "| Heading | Ok |\n| --- | --- |\n| a | b |\n";
    const int word_w = 260;
    uint8_t *word_px = calloc((size_t)word_w * 400, 4);
    uint8_t *solo_px = calloc((size_t)word_w * 120, 4);
    expect(word_px != NULL && solo_px != NULL, "word column buffers");
    if (word_px != NULL && solo_px != NULL) {
        int word_right = 0;
        int cell_right = 0;
        page_set_editing(0);
        page_draw(solo_px, word_w, 120, 0, 0, word_w, 120, "Heading", 7, 7, 7, 28);
        page_draw(word_px, word_w, 400, 0, 0, word_w, 400, one_word, strlen(one_word), strlen(one_word),
                  strlen(one_word), 28);
        for (int y = 0; y < 80; y++) {
            for (int x = 0; x < word_w; x++) {
                const uint8_t *p = solo_px + ((size_t)y * (size_t)word_w + (size_t)x) * 4;
                if (p[0] < 80 && p[1] < 80 && p[2] < 80 && x > word_right) {
                    word_right = x;
                }
            }
        }
        for (int y = 0; y < 160; y++) {
            for (int x = 0; x < word_w; x++) {
                const uint8_t *p = word_px + ((size_t)y * (size_t)word_w + (size_t)x) * 4;
                if (p[0] > PAGE_HEAD_R - 12 && p[0] < PAGE_HEAD_R + 12 && p[1] > PAGE_HEAD_G - 12 &&
                    p[1] < PAGE_HEAD_G + 12 && p[2] > PAGE_HEAD_B - 12 && p[2] < PAGE_HEAD_B + 12 && x > cell_right) {
                    cell_right = x;
                }
            }
        }
        expect(word_right > 40, "Heading has a measurable width");
        expect(cell_right + 2 >= word_right, "the Heading column keeps the whole word");
    }
    free(word_px);
    free(solo_px);

    const char *struck = "~~H~~\nZ";
    page_draw(px, width, height, 0, 0, width, height, struck, strlen(struck), strlen(struck), strlen(struck), font_px);
    expect(count_exact(px, width, height, PAGE_STRIKE_R, PAGE_STRIKE_G, PAGE_STRIKE_B) > 4, "a strike draws a line");
    {
        const char *plain = "x\n";
        const char *ex = "~~x~~\n";
        int ink_top = -1;
        int ink_bot = -1;
        int bar_y = -1;
        int bar_n = 0;
        int skip = page_margin(font_px) + 8;
        page_set_editing(0);
        page_draw(px, width, height, 0, 0, width, height, plain, strlen(plain), 0, 0, font_px);
        for (int y = 0; y < height; y++) {
            for (int col = skip; col < width; col++) {
                const uint8_t *p = px + ((size_t)y * (size_t)width + (size_t)col) * 4;
                if (p[0] < 80 && p[1] < 80 && p[2] < 80) {
                    if (ink_top < 0) {
                        ink_top = y;
                    }
                    ink_bot = y;
                }
            }
        }
        page_draw(px, width, height, 0, 0, width, height, ex, strlen(ex), 0, 0, font_px);
        for (int y = 0; y < height; y++) {
            int n = 0;
            for (int col = skip; col < width; col++) {
                const uint8_t *p = px + ((size_t)y * (size_t)width + (size_t)col) * 4;
                if (p[0] > PAGE_STRIKE_R - 18 && p[0] < PAGE_STRIKE_R + 18 && p[1] > PAGE_STRIKE_G - 18 &&
                    p[1] < PAGE_STRIKE_G + 18 && p[2] > PAGE_STRIKE_B - 18 && p[2] < PAGE_STRIKE_B + 18) {
                    n++;
                }
            }
            if (n > bar_n) {
                bar_n = n;
                bar_y = y;
            }
        }
        expect(ink_top >= 0 && ink_bot > ink_top && bar_y >= 0, "a struck letter and its line are both drawn");
        if (ink_top >= 0 && bar_y >= 0) {
            int ink_mid = (ink_top + ink_bot) / 2;
            int delta = bar_y - ink_mid;
            if (delta < 0) {
                delta = -delta;
            }
            expect(delta <= 3, "the strike sits on the middle of the letter");
        }
    }
    {
        const char *wide_strike = "~~HHHHHHHHHHHHHHHHHHHHHHHHHHHH~~\n";
        const int narrow = 160;
        int edge = narrow - page_margin(font_px);
        uint8_t *clip_px = calloc((size_t)narrow * 160, 4);
        expect(clip_px != NULL, "strike clip buffer");
        if (clip_px != NULL) {
            int past = 0;
            page_set_editing(0);
            page_draw(clip_px, narrow, 160, 0, 0, narrow, 160, wide_strike, strlen(wide_strike),
                      strlen(wide_strike), strlen(wide_strike), font_px);
            for (int y = 0; y < 160; y++) {
                for (int col = edge; col < narrow; col++) {
                    const uint8_t *p = clip_px + ((size_t)y * (size_t)narrow + (size_t)col) * 4;
                    if (p[0] == PAGE_STRIKE_R && p[1] == PAGE_STRIKE_G && p[2] == PAGE_STRIKE_B) {
                        past++;
                    }
                }
            }
            expect(past == 0, "a struck word stops at the column");
            free(clip_px);
        }
    }

    const char *image = "![A](u)\nZ";
    page_set_editing(1);
    page_draw(px, width, height, 0, 0, width, height, image, strlen(image), 0, 0, font_px);
    int image_source = 0;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            image_source += ink_at(px, width, x, y);
        }
    }
    page_draw(px, width, height, 0, 0, width, height, image, strlen(image), strlen(image), strlen(image), font_px);
    int image_rendered = 0;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            image_rendered += ink_at(px, width, x, y);
        }
    }
    expect(near_rgb(px, width, height, PAGE_LINK_R, PAGE_LINK_G, PAGE_LINK_B), "an image shows its alt text");
    expect(image_source > image_rendered, "a rendered image hides the markup");

    const char *link = "[Hi](http://example.com)\nZ";
    page_set_editing(1);
    page_draw(px, width, height, 0, 0, width, height, link, strlen(link), 0, 0, font_px);
    int link_source = 0;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            link_source += ink_at(px, width, x, y);
        }
    }
    page_draw(px, width, height, 0, 0, width, height, link, strlen(link), strlen(link), strlen(link), font_px);
    int link_rendered = 0;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            link_rendered += ink_at(px, width, x, y);
        }
    }
    expect(near_rgb(px, width, height, PAGE_LINK_R, PAGE_LINK_G, PAGE_LINK_B), "a rendered link shows its words");
    expect(link_source > link_rendered, "a rendered link hides the address");

    const char *tilde = "~~~\na\n~~~\nZ";
    page_set_editing(1);
    page_draw(px, width, height, 0, 0, width, height, tilde, strlen(tilde), 0, 0, font_px);
    int tilde_open = count_near(px, width, height, PAGE_CODE_R, PAGE_CODE_G, PAGE_CODE_B);
    page_draw(px, width, height, 0, 0, width, height, tilde, strlen(tilde), 4, 4, font_px);
    int tilde_line = count_near(px, width, height, PAGE_CODE_R, PAGE_CODE_G, PAGE_CODE_B);
    expect(tilde_line > 0 && tilde_line >= tilde_open, "a tilde fence stays open on an inner line");

    const char *flat = "- H\nZ";
    const char *nested = "    - H\nZ";
    page_draw(px, width, height, 0, 0, width, height, flat, strlen(flat), strlen(flat), strlen(flat), font_px);
    int flat_x = width;
    page_draw(px, width, height, 0, 0, width, height, nested, strlen(nested), strlen(nested), strlen(nested), font_px);
    int nested_x = width;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            size_t i = ((size_t)y * (size_t)width + (size_t)x) * 4;
            if (px[i] == PAGE_LIST_R && px[i + 1] == PAGE_LIST_G && px[i + 2] == PAGE_LIST_B && x < nested_x) {
                nested_x = x;
            }
        }
    }
    memset(px, 0, (size_t)width * (size_t)height * 4);
    page_draw(px, width, height, 0, 0, width, height, flat, strlen(flat), strlen(flat), strlen(flat), font_px);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            size_t i = ((size_t)y * (size_t)width + (size_t)x) * 4;
            if (px[i] == PAGE_LIST_R && px[i + 1] == PAGE_LIST_G && px[i + 2] == PAGE_LIST_B && x < flat_x) {
                flat_x = x;
            }
        }
    }
    expect(nested_x > flat_x, "a nested list indents its bullet");

    const char *bullet_go = "- Go\nZ";
    const char *ordered_go = "12. Go\nZ";
    page_draw(px, width, height, 0, 0, width, height, bullet_go, strlen(bullet_go), strlen(bullet_go), strlen(bullet_go), font_px);
    int bullet_lo = width;
    int bullet_hi = 0;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            size_t i = ((size_t)y * (size_t)width + (size_t)x) * 4;
            if (px[i] == PAGE_LIST_R && px[i + 1] == PAGE_LIST_G && px[i + 2] == PAGE_LIST_B) {
                if (x < bullet_lo) {
                    bullet_lo = x;
                }
                if (x > bullet_hi) {
                    bullet_hi = x;
                }
            }
        }
    }
    page_draw(px, width, height, 0, 0, width, height, ordered_go, strlen(ordered_go), strlen(ordered_go), strlen(ordered_go), font_px);
    int ordered_lo = width;
    int ordered_hi = 0;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            size_t i = ((size_t)y * (size_t)width + (size_t)x) * 4;
            if (px[i] == PAGE_LIST_R && px[i + 1] == PAGE_LIST_G && px[i + 2] == PAGE_LIST_B) {
                if (x < ordered_lo) {
                    ordered_lo = x;
                }
                if (x > ordered_hi) {
                    ordered_hi = x;
                }
            }
        }
    }
    expect(ordered_hi > ordered_lo && ordered_hi - ordered_lo > bullet_hi - bullet_lo, "an ordered item keeps its number");

    const char *em_line = "*Hi*\nZ";
    page_draw(px, width, height, 0, 0, width, height, em_line, strlen(em_line), strlen(em_line), strlen(em_line), font_px);
    expect(near_rgb(px, width, height, PAGE_EM_R, PAGE_EM_G, PAGE_EM_B), "emphasis stays colored");
    const char *inside = "a_b_c\nZ";
    page_draw(px, width, height, 0, 0, width, height, inside, strlen(inside), strlen(inside), strlen(inside), font_px);
    expect(!near_rgb(px, width, height, PAGE_EM_R, PAGE_EM_G, PAGE_EM_B), "an underscore inside a word is not emphasis");
    const char *plain_go = "Go\nZ";
    page_draw(px, width, height, 0, 0, width, height, plain_go, strlen(plain_go), strlen(plain_go), strlen(plain_go), font_px);
    int plain_ink = count_near(px, width, height, PAGE_INK_R, PAGE_INK_G, PAGE_INK_B);
    const char *heavy = "**Go**\nZ";
    page_draw(px, width, height, 0, 0, width, height, heavy, strlen(heavy), strlen(heavy), strlen(heavy), font_px);
    int heavy_ink = count_near(px, width, height, PAGE_INK_R, PAGE_INK_G, PAGE_INK_B);
    expect(!near_rgb(px, width, height, PAGE_EM_R, PAGE_EM_G, PAGE_EM_B), "strong is not the emphasis color");
    expect(heavy_ink > plain_ink, "strong is heavier than the same letters");

    const char *code_line = "`Go`\nZ";
    page_set_editing(1);
    page_draw(px, width, height, 0, 0, width, height, code_line, strlen(code_line), 0, 0, font_px);
    int code_source = 0;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            code_source += ink_at(px, width, x, y);
        }
    }
    page_draw(px, width, height, 0, 0, width, height, code_line, strlen(code_line), strlen(code_line), strlen(code_line), font_px);
    int code_rendered = 0;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            code_rendered += ink_at(px, width, x, y);
        }
    }
    expect(near_rgb(px, width, height, PAGE_CODE_R, PAGE_CODE_G, PAGE_CODE_B), "rendered code keeps its word");
    expect(code_source > code_rendered, "rendered code hides its ticks");

    page_set_readonly(1);
    page_draw(px, width, height, 0, 0, width, height, head, strlen(head), 0, 0, font_px);
    int head_ro = count_near(px, width, height, PAGE_HEAD_R, PAGE_HEAD_G, PAGE_HEAD_B);
    page_set_readonly(0);
    page_draw(px, width, height, 0, 0, width, height, head, strlen(head), 0, 0, font_px);
    int head_live = count_near(px, width, height, PAGE_HEAD_R, PAGE_HEAD_G, PAGE_HEAD_B);
    expect(head_live > head_ro, "read only hides marks on the caret line");

    page_set_line(1);
    page_draw(px, width, height, 0, 0, width, height, "H", 1, 1, 1, font_px);
    expect(near_rgb(px, width, height, PAGE_INK_R, PAGE_INK_G, PAGE_INK_B), "the caret line highlight leaves the letter visible");
    int band_y = page_margin(font_px) + 2;
    const uint8_t *wash = px + ((size_t)band_y * (size_t)width + 4) * 4;
    expect(wash[0] != PAGE_PAPER_R || wash[1] != PAGE_PAPER_G || wash[2] != PAGE_PAPER_B,
           "the caret line highlight is a wash");
    page_set_line(0);
    page_draw(px, width, height, 0, 0, width, height, "H", 1, 1, 1, font_px);
    wash = px + ((size_t)band_y * (size_t)width + 4) * 4;
    expect(wash[0] == PAGE_PAPER_R && wash[1] == PAGE_PAPER_G && wash[2] == PAGE_PAPER_B,
           "the caret line highlight turns off");
    page_set_editing(0);

    const char *counted = "one two\nthree";
    page_draw(px, width, height, 0, 0, width, height, counted, strlen(counted), 0, 0, font_px);
    expect(page_words() == 3 && page_lines() == 2, "three words on two lines");
    const char *marks = "# Hi\n---\n";
    page_draw(px, width, height, 0, 0, width, height, marks, strlen(marks), 0, 0, font_px);
    expect(page_words() == 1, "marks are not words");
    const char *heads = "a\n# B\nc\n# D\n";
    expect(page_heading(heads, strlen(heads), 0, 1) == 2, "the next heading is B");
    expect(page_heading(heads, strlen(heads), 2, 1) == 8, "the heading after B is D");
    expect(page_heading(heads, strlen(heads), 8, -1) == 2, "the heading before D is B");

    page_set_editing(1);
    const char *escaped = "\\*Hi\\*\nZ";
    page_draw(px, width, height, 0, 0, width, height, escaped, strlen(escaped), strlen(escaped), strlen(escaped),
              font_px);
    expect(!near_rgb(px, width, height, PAGE_EM_R, PAGE_EM_G, PAGE_EM_B), "an escaped star is not emphasis");
    const char *address = "<https://penship.app>\nZ";
    page_draw(px, width, height, 0, 0, width, height, address, strlen(address), 0, 0, font_px);
    int address_source = 0;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            address_source += ink_at(px, width, x, y);
        }
    }
    page_draw(px, width, height, 0, 0, width, height, address, strlen(address), strlen(address), strlen(address),
              font_px);
    int address_rendered = 0;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            address_rendered += ink_at(px, width, x, y);
        }
    }
    expect(near_rgb(px, width, height, PAGE_LINK_R, PAGE_LINK_G, PAGE_LINK_B), "an autolink is a link");
    expect(address_source > address_rendered, "an autolink hides its brackets off the caret line");
    const char *indented = "    code\nZ";
    page_set_editing(1);
    page_draw(px, width, height, 0, 0, width, height, indented, strlen(indented), strlen(indented), strlen(indented),
              font_px);
    expect(near_rgb(px, width, height, PAGE_CODE_R, PAGE_CODE_G, PAGE_CODE_B), "indented code keeps its color");
    expect(near_rgb(px, width, height, PAGE_BLOCK_R, PAGE_BLOCK_G, PAGE_BLOCK_B), "indented code keeps the wash");
    const char *nested_list = "    - item\nZ";
    page_draw(px, width, height, 0, 0, width, height, nested_list, strlen(nested_list), strlen(nested_list),
              strlen(nested_list), font_px);
    expect(count_exact(px, width, height, PAGE_LIST_R, PAGE_LIST_G, PAGE_LIST_B) > 8, "an indented marker stays a list");
    const char *ref = "[Hi][id]\n\n[id]: https://example.com\nZ";
    page_draw(px, width, height, 0, 0, width, height, ref, strlen(ref), strlen(ref), strlen(ref), font_px);
    expect(near_rgb(px, width, height, PAGE_LINK_R, PAGE_LINK_G, PAGE_LINK_B), "a reference link shows its words");
    int ref_rendered = 0;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            ref_rendered += ink_at(px, width, x, y);
        }
    }
    page_draw(px, width, height, 0, 0, width, height, ref, strlen(ref), 0, 0, font_px);
    int ref_source = 0;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            ref_source += ink_at(px, width, x, y);
        }
    }
    expect(ref_source > ref_rendered, "the caret line shows the reference marks");
    const char *missing = "[Nope]\nZ";
    page_draw(px, width, height, 0, 0, width, height, missing, strlen(missing), strlen(missing), strlen(missing),
              font_px);
    expect(!near_rgb(px, width, height, PAGE_LINK_R, PAGE_LINK_G, PAGE_LINK_B), "an undefined shortcut stays text");
    const char *mail = "<me@penship.app>\nZ";
    page_draw(px, width, height, 0, 0, width, height, mail, strlen(mail), strlen(mail), strlen(mail), font_px);
    expect(near_rgb(px, width, height, PAGE_LINK_R, PAGE_LINK_G, PAGE_LINK_B), "an email autolink is a link");
    page_set_editing(0);
    free(px);
}

static void test_selection_paint(const unsigned char *font, int font_len) {
    expect(page_set_font(font, font_len) == 0, "selection font");
    const int width = 480;
    const int height = 160;
    uint8_t *px = calloc((size_t)width * (size_t)height, 4);
    expect(px != NULL, "selection alloc");
    if (px == NULL) {
        return;
    }
    page_draw(px, width, height, 0, 0, width, height, "X Y", 3, 2, 1, 32);
    expect(near_rgb(px, width, height, PAGE_SEL_R, PAGE_SEL_G, PAGE_SEL_B), "selection covers the space");
    page_draw(px, width, height, 0, 0, width, height, "X Y", 3, 3, 3, 32);
    expect(!near_rgb(px, width, height, PAGE_SEL_R, PAGE_SEL_G, PAGE_SEL_B), "no selection paints no wash");
    free(px);
}

static void test_label(const unsigned char *font, int font_len) {
    expect(page_set_font(font, font_len) == 0, "label font");
    const int width = 400;
    const int height = 200;
    uint8_t *px = calloc((size_t)width * (size_t)height, 4);
    expect(px != NULL, "label alloc");
    if (px == NULL) {
        return;
    }
    int xs[] = {40};
    int ys[] = {30};
    int ws[] = {200};
    int hs[] = {80};
    const char *labels[] = {"Font"};
    page_draw_labels(px, width, height, 1, xs, ys, ws, hs, labels, 0, 32, 0, height);
    expect(near_rgb(px, width, height, PAGE_INK_R, PAGE_INK_G, PAGE_INK_B), "a label is drawn");
    expect(px[0] == 0 && px[1] == 0 && px[2] == 0, "a label stays inside its cell");
    memset(px, 0, (size_t)width * (size_t)height * 4);
    const char *empty[] = {""};
    page_draw_labels(px, width, height, 1, xs, ys, ws, hs, empty, 0, 32, 0, height);
    expect(px[0] == 0, "an empty label draws nothing");
    free(px);
}

static int px_eq(const uint8_t *px, int width, int x, int y, int r, int g, int b) {
    const uint8_t *p = px + ((size_t)y * (size_t)width + (size_t)x) * 4;
    return p[0] == (uint8_t)r && p[1] == (uint8_t)g && p[2] == (uint8_t)b && p[3] == 255;
}

static void test_menu(void) {
    const int width = 96;
    const int height = 96;
    const int x0 = 8;
    const int y0 = 8;
    const int side = 64;
    uint8_t *px = calloc((size_t)width * (size_t)height, 4);
    expect(px != NULL, "menu alloc");
    if (px == NULL) {
        return;
    }
    page_set_theme(0);
    page_draw_menu(px, width, height, x0, y0, side);
    int cx = x0 + side / 2;
    int cy = y0 + side / 2;
    int thick = side / 9;
    expect(px_eq(px, width, cx, cy, PAGE_INK_R, PAGE_INK_G, PAGE_INK_B), "the middle bar is ink");
    expect(px_eq(px, width, cx, cy - thick, PAGE_PAPER_R, PAGE_PAPER_G, PAGE_PAPER_B), "a gap between bars stays the button");
    expect(px[((size_t)y0 * (size_t)width + (size_t)x0) * 4] == 0, "the corner outside the disc stays clear");
    int ink = 0;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            if (px_eq(px, width, x, y, PAGE_INK_R, PAGE_INK_G, PAGE_INK_B)) {
                ink++;
            }
        }
    }
    expect(ink > thick * (side / 2), "the menu is three bars");
    memset(px, 0, (size_t)width * (size_t)height * 4);
    page_set_theme(1);
    page_draw_menu(px, width, height, x0, y0, side);
    expect(px_eq(px, width, cx, cy - thick, PAGE_NIGHT_PAPER_R, PAGE_NIGHT_PAPER_G, PAGE_NIGHT_PAPER_B),
           "the gap follows the night paper");
    expect(px_eq(px, width, cx, cy, PAGE_NIGHT_INK_R, PAGE_NIGHT_INK_G, PAGE_NIGHT_INK_B),
           "a bar follows the night ink");
    page_set_theme(0);
    memset(px, 0, (size_t)width * (size_t)height * 4);
    int tools_ok = 1;
    for (int tool = 0; tool < 12; tool++) {
        memset(px, 0, (size_t)width * (size_t)height * 4);
        page_draw_tool(px, width, height, 8, 8, 64, 64, tool);
        int mark = 0;
        for (int y = 8; y < 72; y++) {
            for (int x = 8; x < 72; x++) {
                const uint8_t *p = px + ((size_t)y * (size_t)width + (size_t)x) * 4;
                if (p[0] != 0 || p[1] != 0 || p[2] != 0) {
                    mark++;
                }
            }
        }
        if (mark < 30 || mark > 64 * 64 / 2) {
            tools_ok = 0;
        }
    }
    expect(tools_ok, "each format mark is a stroke, not a filled block");
    free(px);
}

static void test_modes(const unsigned char *font, int font_len) {
    expect(page_set_font(font, font_len) == 0, "modes font");
    const int width = 240;
    const int height = 400;
    const int font_px = 32;
    const int n = 40;
    char *text = malloc((size_t)n);
    uint8_t *px = calloc((size_t)width * (size_t)height, 4);
    expect(text != NULL && px != NULL, "modes alloc");
    if (text == NULL || px == NULL) {
        free(text);
        free(px);
        return;
    }
    memset(text, '\n', (size_t)n);
    page_set_typewriter(1);
    page_draw(px, width, height, 0, 0, width, height, text, (size_t)n, (size_t)n, (size_t)n, font_px);
    int above = 0;
    int below = 0;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            if (x >= width - 12 || !ink_at(px, width, x, y)) {
                continue;
            }
            if (y < height / 2) {
                above++;
            } else {
                below++;
            }
        }
    }
    expect(above > 0 && below > 0, "typewriter keeps the caret in the middle");
    memset(px, 0, (size_t)width * (size_t)height * 4);
    page_draw(px, width, height, 0, 0, width, height, text, (size_t)n, 0, 0, font_px);
    int top = 0;
    int mid = 0;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            if (x >= width - 12 || !ink_at(px, width, x, y)) {
                continue;
            }
            if (y < height / 3) {
                top++;
            } else if (y > height / 3 && y < height * 2 / 3) {
                mid++;
            }
        }
    }
    expect(mid > top, "the first line sits in the center band");
    expect(page_center_drag(100000) > 0, "a downward drag of the text moves later rows");
    expect(page_center_drag(-100000) < 0, "an upward drag of the text moves earlier rows");
    page_set_typewriter(0);

    const char *head = "# H\nZ";
    page_set_focus(0);
    page_draw(px, width, height, 0, 0, width, height, head, strlen(head), strlen(head), strlen(head), font_px);
    int bright = count_near(px, width, height, PAGE_HEAD_R, PAGE_HEAD_G, PAGE_HEAD_B);
    page_set_focus(1);
    page_draw(px, width, height, 0, 0, width, height, head, strlen(head), strlen(head), strlen(head), font_px);
    int dim = count_near(px, width, height, PAGE_HEAD_R, PAGE_HEAD_G, PAGE_HEAD_B);
    expect(bright > 0 && dim < bright, "focus dims every line but the caret line");
    page_set_focus(0);

    page_set_theme(1);
    page_draw(px, width, height, 0, 0, width, height, "H", 1, 1, 1, font_px);
    expect(px[0] == PAGE_NIGHT_PAPER_R && px[1] == PAGE_NIGHT_PAPER_G && px[2] == PAGE_NIGHT_PAPER_B,
           "night paper");
    expect(near_rgb(px, width, height, PAGE_NIGHT_INK_R, PAGE_NIGHT_INK_G, PAGE_NIGHT_INK_B), "night ink");
    page_set_theme(0);
    free(text);
    free(px);
}

static void test_scroll(const unsigned char *font, int font_len) {
    expect(page_set_font(font, font_len) == 0, "set font again");
    const int width = 200;
    const int height = 400;
    char text[64];
    int n = 0;
    for (int i = 0; i < 40; i++) {
        text[n++] = '\n';
    }
    uint8_t *px = calloc((size_t)width * (size_t)height, 4);
    page_draw(px, width, height, 0, 0, width, height, text, (size_t)n, (size_t)n, (size_t)n, 32);
    int ink = 0;
    int above = 0;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            if (!ink_at(px, width, x, y)) {
                continue;
            }
            ink++;
            if (y < height / 2) {
                above++;
            }
        }
    }
    expect(ink > 8, "scrolled caret is visible");
    expect(above == 0, "scrolled caret is in the lower half");
    free(px);
}

static void test_glide(void) {
    page_set_glide(2);
    expect(page_fling_arm(8000.f, 160.f) == 1, "a browser flick starts");
    expect(page_fling_at(0) == 0.f, "a fling starts at the finger");
    float early = page_fling_at(180);
    float all = page_fling_at(100000);
    expect(all > 4000.f && early > 0.f && early < all, "medium carries like a long page");
    expect(page_fling_step(0) == 0.f, "the first instant does not jump");
    float moved = page_fling_step(180);
    expect(moved > 100.f, "the fling has moved by then");
    expect(page_fling_step(180) == 0.f, "the same moment is not applied twice");
    page_set_glide(1);
    expect(page_fling_arm(8000.f, 160.f) == 1, "a short flick starts");
    float short_d = page_fling_at(100000);
    page_set_glide(4);
    expect(page_fling_arm(8000.f, 160.f) == 1, "a far flick starts");
    float far_d = page_fling_at(100000);
    expect(far_d > short_d * 2.f, "far carries much farther than short");
    page_set_glide(0);
    expect(page_fling_arm(8000.f, 160.f) == 0, "glide off does not fling");
    page_set_glide(2);
}

static void test_find(void) {
    const char *text = "Alpha beta ALPHA";
    size_t n = strlen(text);
    expect(page_find(text, n, "alpha", 5, 0, 1) == 0, "find ignores ASCII case");
    expect(page_find(text, n, "alpha", 5, 1, 1) == 11, "find continues after the caret");
    expect(page_find(text, n, "alpha", 5, n, 1) == 0, "find wraps to the start");
    expect(page_find(text, n, "alpha", 5, 11, -1) == 0, "find backward stops before the caret");
    expect(page_find(text, n, "alpha", 5, 0, -1) == 11, "find backward wraps");
    expect(page_find(text, n, "", 0, 0, 1) == n, "an empty find misses");
    expect(page_find(text, n, "nope", 4, 0, 1) == n, "a missing word misses");
    const char *utf = "caf\xc3\xa9";
    expect(page_find(utf, 5, "\xa9", 1, 0, 1) == 5, "a find does not start on a continuation byte");
}

static void test_heads(void) {
    struct PageHead heads[4];
    const char *note = "a\n# B\n```\n# nope\n```\n## Cee\nTitle\n====\n";
    int n = page_heads(note, strlen(note), heads, 4);
    expect(n == 3, "three headings outside the fence");
    expect(heads[0].at == 2 && heads[0].level == 1 && strcmp(heads[0].title, "B") == 0, "first heading is B");
    expect(heads[1].at == 21 && heads[1].level == 2 && strcmp(heads[1].title, "Cee") == 0, "second heading is Cee");
    expect(heads[2].at == 28 && heads[2].level == 1 && strcmp(heads[2].title, "Title") == 0, "setext heading");
    expect(page_heads(note, strlen(note), heads, 1) == 1, "the list stops at the cap");
    expect(page_heads(note, strlen(note), NULL, 4) == 0, "a null list is refused");
    expect(page_heads(NULL, 3, heads, 4) == 0, "a null note is empty");
    const char *closed = "# Hi #\n";
    expect(page_heads(closed, strlen(closed), heads, 1) == 1 && strcmp(heads[0].title, "Hi") == 0,
           "closing hashes stay out of the title");
    const char *stuck = "#Hi\n";
    expect(page_heads(stuck, strlen(stuck), heads, 1) == 0, "a hash without a space is not a heading");
    char cut[80];
    cut[0] = '#';
    cut[1] = ' ';
    for (int i = 0; i < 70; i++) {
        cut[2 + i] = 'a';
    }
    cut[72] = (char)0xc3;
    cut[73] = (char)0xa9;
    cut[74] = '\n';
    expect(page_heads(cut, 75, heads, 1) == 1, "a heading with a trailing e-acute");
    size_t tn = strlen(heads[0].title);
    expect(tn == 70 && heads[0].title[tn] == '\0', "the title stops before a split code point");
    const char *path = "intro\n# A\n## B\n### C\nbody\n## D\n";
    struct PageHead trail[6];
    size_t body = (size_t)(strstr(path, "body") - path);
    size_t dee = (size_t)(strstr(path, "## D") - path);
    int depth = page_trail(path, strlen(path), body, trail, 6);
    expect(depth == 3, "the body sits under three headings");
    expect(strcmp(trail[0].title, "A") == 0 && trail[0].level == 1, "the outer heading is A");
    expect(strcmp(trail[1].title, "B") == 0 && trail[1].level == 2, "the middle heading is B");
    expect(strcmp(trail[2].title, "C") == 0 && trail[2].level == 3, "the inner heading is C");
    depth = page_trail(path, strlen(path), dee, trail, 6);
    expect(depth == 2 && strcmp(trail[0].title, "A") == 0 && strcmp(trail[1].title, "D") == 0,
           "a new section closes the inner headings");
    expect(page_trail(path, strlen(path), 0, trail, 6) == 0, "text before the first heading has no trail");
    expect(page_sections() == 1, "the section trail starts on");
    page_set_sections(0);
    expect(page_sections() == 0, "the section trail can be turned off");
    page_set_sections(1);
}

static void test_plate(void) {
    const int width = 40;
    const int height = 30;
    uint8_t *px = calloc((size_t)width * (size_t)height, 4);
    expect(px != NULL, "plate buffer");
    if (px == NULL) {
        return;
    }
    for (int i = 0; i < width * height; i++) {
        px[i * 4] = 1;
        px[i * 4 + 1] = 2;
        px[i * 4 + 2] = 3;
    }
    page_set_theme(0);
    page_draw_plate(px, width, height, 4, 4, 20, 16, 2);
    const uint8_t *mid = px + ((size_t)8 * (size_t)width + 10) * 4;
    const uint8_t *edge = px + ((size_t)4 * (size_t)width + 10) * 4;
    const uint8_t *rule = px + ((size_t)12 * (size_t)width + 10) * 4;
    const uint8_t *out = px + ((size_t)2 * (size_t)width + 2) * 4;
    expect(mid[0] == PAGE_PAPER_R && mid[1] == PAGE_PAPER_G && mid[2] == PAGE_PAPER_B, "the plate covers the page");
    expect(edge[0] == PAGE_RULE_R && edge[1] == PAGE_RULE_G && edge[2] == PAGE_RULE_B, "the plate has a solid edge");
    expect(rule[0] == PAGE_RULE_R && rule[1] == PAGE_RULE_G && rule[2] == PAGE_RULE_B, "the rows are split by a rule");
    expect(out[0] == 1 && out[1] == 2 && out[2] == 3, "the plate leaves the rest of the page");
    page_set_theme(1);
    page_draw_plate(px, width, height, 4, 4, 20, 16, 2);
    expect(mid[0] == PAGE_NIGHT_PAPER_R && mid[1] == PAGE_NIGHT_PAPER_G && mid[2] == PAGE_NIGHT_PAPER_B,
           "night paper is solid too");
    page_set_theme(0);
    free(px);
}

static void test_picture(const unsigned char *font, int font_len) {
    uint8_t rgba[16];
    const char *text = "![a small ship](ship.png)\n";
    const int width = 240;
    const int height = 200;
    uint8_t *px = calloc((size_t)width * (size_t)height, 4);
    int red = 0;
    expect(page_set_font(font, font_len) == 0, "picture font");
    for (int i = 0; i < 4; i++) {
        rgba[i * 4] = 255;
        rgba[i * 4 + 1] = 0;
        rgba[i * 4 + 2] = 0;
        rgba[i * 4 + 3] = 255;
    }
    expect(page_set_picture("ship.png", rgba, 2, 2) == 0, "the picture is kept");
    page_set_editing(0);
    page_scroll_place(0.f);
    expect(px != NULL, "picture buffer");
    if (px != NULL) {
        page_draw(px, width, height, 0, 0, width, height, text, strlen(text), strlen(text), strlen(text), 28);
        for (int i = 0; i < width * height; i++) {
            if (px[i * 4] > 200 && px[i * 4 + 1] < 40 && px[i * 4 + 2] < 40) {
                red++;
            }
        }
        expect(red > 400, "a figure line fills the column");
        free(px);
    }
}

int main(void) {
    test_plate();
    test_heads();
    test_find();
    test_glide();
    int font_len = 0;
    unsigned char *font = load_font(&font_len);
    if (font != NULL) {
        test_picture(font, font_len);
        test_caret_and_glyph(font, font_len);
        test_hit(font, font_len);
        test_link_url();
        test_word_wrap(font, font_len);
        test_table_cell(font, font_len);
        test_hit_rendered_mark(font, font_len);
        test_user_scroll(font, font_len);
        test_markdown(font, font_len);
        test_rendered(font, font_len);
        test_modes(font, font_len);
        test_label(font, font_len);
        test_menu();
        test_selection_paint(font, font_len);
        test_tall_note(font, font_len);
        test_scroll(font, font_len);
        free(font);
    }
    if (fail) {
        return 1;
    }
    puts("page_test ok");
    return 0;
}
