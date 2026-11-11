#include "chrome.h"

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

static int thumb_of(int density) {
    int thumb = density * 48 / 160;
    if (thumb < 48) {
        thumb = 48;
    }
    return thumb;
}

static void expect_placed(const struct Chrome *chrome, int index, int width, int height) {
    expect(chrome->w[index] > 0 && chrome->h[index] > 0, chrome_label(index));
    expect(chrome->x[index] >= 0 && chrome->y[index] >= 0, "control is on screen");
    expect(chrome->x[index] + chrome->w[index] <= width, "control stays inside the width");
    expect(chrome->y[index] + chrome->h[index] <= height, "control stays inside the height");
    expect(chrome_hit(chrome, chrome->x[index] + chrome->w[index] / 2, chrome->y[index] + chrome->h[index] / 2) ==
               index,
           chrome_label(index));
}

static void test_phone(void) {
    struct Chrome chrome;
    const int width = 1080;
    const int height = 2400;
    const int density = 420;
    chrome_place(&chrome, width, height, 80, 160, density, 0, 1000, NULL, NULL, 0);
    expect(chrome.vertical == 0, "the writing surface is not a column of options");
    expect(chrome.text_w == width, "the page uses the full width");
    expect(chrome.w[CHROME_BOLD] == 0, "the reading page has no format row");
    expect(chrome.text_y == 80, "the column starts at the status inset");
    expect(chrome.text_h == height - 80 - 160, "the column fills the space between the insets");
    expect(chrome.y[CHROME_SETTINGS] < chrome.text_y + chrome.text_h, "the menu mark floats on the column");
    chrome_place(&chrome, width, height, 80, 160, density, 0, 1000, NULL, NULL, 1);
    expect(chrome.h[CHROME_BOLD] == thumb_of(density), "the formatting row is a thumb tall");
    expect(chrome.w[CHROME_BOLD] == width / 12, "the twelve marks share the width");
    expect(chrome.text_y + chrome.text_h <= chrome.y[CHROME_BOLD], "the column stops above the marks");
    expect(chrome.y[CHROME_BOLD] - (chrome.text_y + chrome.text_h) > 0, "a gap sits between the type and the marks");
    expect(chrome.y[CHROME_BOLD] + chrome.h[CHROME_BOLD] == height - 160, "the marks sit above the bottom inset");
    expect(chrome.y[CHROME_ITALIC] == chrome.y[CHROME_BOLD] && chrome.x[CHROME_ITALIC] > chrome.x[CHROME_BOLD],
           "italic sits beside bold");
    expect(chrome.x[CHROME_STRIKE] > chrome.x[CHROME_ITALIC] && chrome.x[CHROME_CODE] > chrome.x[CHROME_STRIKE],
           "strike and code follow along the row");
    expect(chrome.x[CHROME_LINK] > chrome.x[CHROME_CODE] && chrome.x[CHROME_BLOCK] > chrome.x[CHROME_LINK],
           "the rest of the bar continues past the edge");
    expect(strcmp(chrome_label(CHROME_LINK), "Link") == 0, "link label");
    expect(strcmp(chrome_label(CHROME_BLOCK), "Block") == 0, "block label");
    int bar = 100000;
    chrome_place(&chrome, width, height, 80, 160, density, 0, 1000, NULL, &bar, 1);
    expect(bar == 0 && chrome.x[CHROME_BLOCK] + chrome.w[CHROME_BLOCK] == width, "a phone bar keeps every mark on screen");
    expect(strcmp(chrome_label(CHROME_BOLD), "Bold") == 0, "bold label");
    expect(strcmp(chrome_label(CHROME_ITALIC), "Italic") == 0, "italic label");
    expect(strcmp(chrome_label(CHROME_STRIKE), "Strike") == 0, "strike label");
    expect(strcmp(chrome_label(CHROME_CODE), "Code") == 0, "code label");
    expect(chrome_bar_px(160, 1000) == 18, "the bar is 18sp");
    expect_placed(&chrome, CHROME_SETTINGS, width, height);
    expect(chrome.w[CHROME_SMALLER] == 0 && chrome.w[CHROME_FONT] == 0 && chrome.w[CHROME_SAVE_AS] == 0,
           "options wait in the settings view");
    int thumb = thumb_of(density);
    expect(chrome.w[CHROME_SETTINGS] <= thumb * 2 && chrome.h[CHROME_SETTINGS] <= thumb * 2,
           "the menu is a small floating button");
    expect(chrome.x[CHROME_SETTINGS] + chrome.w[CHROME_SETTINGS] > width - thumb, "the menu sits at the end");
    expect(chrome.y[CHROME_SETTINGS] <= 80 + thumb, "the menu sits under the status inset");
    expect(chrome.y[CHROME_SETTINGS] + chrome.h[CHROME_SETTINGS] <= height - 160, "the menu sits above the bottom inset");
    expect(chrome_hit(&chrome, 40, chrome.text_y + 40) == -1, "the page is not a control");
    expect(chrome_label(CHROME_SETTINGS)[0] == 'M', "menu label");
    expect(chrome_label(CHROME_OPEN)[0] == 'O', "open label");
    expect(strcmp(chrome_label(CHROME_SAVE), "Save") == 0, "save label");
    expect(strcmp(chrome_label(CHROME_SAVE_AS), "Save as") == 0, "save as label");
    expect(chrome_label(CHROME_EXPORT)[0] == 'E', "export label");
    expect(strcmp(chrome_label(CHROME_CONTENTS), "Contents") == 0, "contents label");
}

static void test_settings_view(void) {
    struct Chrome chrome;
    const int width = 1080;
    const int height = 2400;
    const int density = 420;
    chrome_place(&chrome, width, height, 80, 160, density, 1, 1000, NULL, NULL, 1);
    expect(chrome.w[CHROME_SETTINGS] > 0, "the menu mark stays while the list is open");
    expect(chrome.w[CHROME_DONE] == 0, "the menu has no done row");
    int options[] = {CHROME_OPEN, CHROME_SAVE, CHROME_SAVE_AS, CHROME_EXPORT, CHROME_CONTENTS,
                     CHROME_FIND, CHROME_OPTIONS};
    int noptions = (int)(sizeof options / sizeof options[0]);
    for (int i = 0; i < noptions; i++) {
        expect_placed(&chrome, options[i], width, height);
        expect(chrome.y[options[i]] >= 80, "an option sits below the status inset");
        expect(chrome.y[options[i]] + chrome.h[options[i]] <= height - 160, "an option sits above the bottom inset");
    }
    expect(chrome.y[CHROME_SAVE] > chrome.y[CHROME_OPEN], "save sits under open");
    expect(chrome.y[CHROME_SAVE_AS] > chrome.y[CHROME_SAVE], "save as sits under save");
    expect(chrome.y[CHROME_EXPORT] > chrome.y[CHROME_SAVE_AS], "export sits under save as");
    expect(chrome.y[CHROME_CONTENTS] > chrome.y[CHROME_EXPORT], "contents sits under export");
    expect(chrome.y[CHROME_FIND] > chrome.y[CHROME_CONTENTS], "find sits under contents");
    expect(strcmp(chrome_label(CHROME_FIND), "Find") == 0, "find label");
    expect(chrome.y[CHROME_OPTIONS] > chrome.y[CHROME_FIND], "settings sits under find");
    expect(chrome.y[CHROME_OPEN] == 80, "the menu list starts at the status inset");
    expect(chrome_hit(&chrome, chrome.x[CHROME_SETTINGS] + chrome.w[CHROME_SETTINGS] / 2,
                      chrome.y[CHROME_SETTINGS] + chrome.h[CHROME_SETTINGS] / 2) == CHROME_SETTINGS,
           "a tap on the mark is the mark where a row passes under it");
    expect(strcmp(chrome_label(CHROME_OPTIONS), "Settings") == 0, "settings label");
    expect(chrome.w[CHROME_SMALLER] == 0 && chrome.w[CHROME_FONT] == 0, "preferences wait on their own screen");
    expect(chrome.w[CHROME_BOLD] == 0 && chrome.w[CHROME_CODE] == 0, "the formatting bar waits for the page");
    chrome_place(&chrome, width, height, 80, 160, density, 2, 1000, NULL, NULL, 1);
    int prefs[] = {CHROME_SMALLER, CHROME_LARGER, CHROME_FONT,  CHROME_FONTFILE, CHROME_GETFACES, CHROME_LISTS,
                   CHROME_TYPEWRITER,
                   CHROME_FOCUS,  CHROME_LINE,   CHROME_SECTIONS, CHROME_BAR, CHROME_GLIDE, CHROME_THEME, CHROME_SOURCE,
                   CHROME_READONLY};
    for (int i = 0; i < (int)(sizeof prefs / sizeof prefs[0]); i++) {
        expect_placed(&chrome, prefs[i], width, height);
    }
    expect(chrome.w[CHROME_OPEN] == 0 && chrome.w[CHROME_FIND] == 0, "the file menu waits behind settings");
    expect(chrome.y[CHROME_FONTFILE] > chrome.y[CHROME_FONT], "font file sits under font");
    expect(chrome.y[CHROME_GETFACES] > chrome.y[CHROME_FONTFILE], "download sits under font file");
    expect(chrome.y[CHROME_LISTS] > chrome.y[CHROME_GETFACES], "font lists sits under download");
    expect(strcmp(chrome_label(CHROME_GETFACES), "Download faces") == 0, "download faces label");
    expect(strcmp(chrome_label(CHROME_LISTS), "Font lists") == 0, "font lists label");
    expect(chrome.head_n == 4, "settings has four group titles");
    expect(strcmp(chrome_label(CHROME_FONTFILE), "Font file") == 0, "font file label");
    expect(chrome.y[CHROME_LINE] > chrome.y[CHROME_FOCUS], "line sits under focus");
    expect(chrome.y[CHROME_SECTIONS] > chrome.y[CHROME_LINE], "sections sits under line");
    expect(chrome.y[CHROME_BAR] > chrome.y[CHROME_SECTIONS], "bar sits under sections");
    expect(chrome.y[CHROME_GLIDE] > chrome.y[CHROME_BAR], "glide sits under bar");
    expect(strcmp(chrome_label(CHROME_SECTIONS), "Sections") == 0, "sections label");
    expect(strcmp(chrome_label(CHROME_GLIDE), "Glide") == 0, "glide label");
    expect(strcmp(chrome_label(CHROME_LINE), "Line") == 0, "line label");
    expect(chrome.y[CHROME_READONLY] > chrome.y[CHROME_SOURCE], "read only sits under source");
    expect(strcmp(chrome_label(CHROME_READONLY), "Read only") == 0, "read only label");
    expect(chrome.w[CHROME_DONE] == 0, "settings has no done row");
    expect(chrome_label(CHROME_FONT)[0] == 'F', "font label");
}

static void test_wide(void) {
    struct Chrome chrome;
    const int width = 2400;
    const int height = 1080;
    const int density = 320;
    chrome_place(&chrome, width, height, 40, 40, density, 0, 1000, NULL, NULL, 0);
    expect(chrome.text_x > 0 && chrome.text_w < width, "a wide page keeps a reading measure");
    expect(chrome.text_x + chrome.text_w / 2 > width / 2 - 2 && chrome.text_x + chrome.text_w / 2 < width / 2 + 2,
           "the wide measure sits in the middle");
    expect(chrome.text_h == height - 40 - 40, "a wide reading page fills the space between the insets");
    expect(chrome.w[CHROME_BOLD] == 0, "a wide reading page hides the format row");
    expect_placed(&chrome, CHROME_SETTINGS, width, height);
    expect(chrome.w[CHROME_SETTINGS] < width / 4, "the menu does not become a bar");
    expect(chrome.y[CHROME_SETTINGS] <= 40 + thumb_of(density), "the wide menu sits under the status inset");
    expect(chrome.w[CHROME_THEME] == 0, "wide writing hides the options");
    expect(chrome.text_y == 40, "the page starts at the status inset");
    chrome_place(&chrome, width, height, 40, 40, density, 0, 1000, NULL, NULL, 1);
    expect(chrome.w[CHROME_BOLD] > thumb_of(density), "a wide bar gives each mark more than a thumb");
    expect(chrome.x[CHROME_BLOCK] + chrome.w[CHROME_BLOCK] == width, "a wide bar ends with the block mark");
    expect(chrome.x[CHROME_BOLD] == 0, "a wide bar starts at the edge");
    int scroll = 100000;
    chrome_place(&chrome, width, height, 40, 40, density, 0, 1000, NULL, &scroll, 1);
    expect(scroll == 0, "a wide bar does not need to scroll");
    chrome_place(&chrome, width, height, 40, 40, density, 1, 1000, NULL, NULL, 0);
    expect(chrome.w[CHROME_OPEN] < width && chrome.x[CHROME_OPEN] > 0, "a wide menu is a centered column");
    expect(chrome.x[CHROME_OPTIONS] == chrome.x[CHROME_OPEN] && chrome.w[CHROME_OPTIONS] == chrome.w[CHROME_OPEN],
           "wide menu rows share the column");
}

static void test_menu_size(void) {
    struct Chrome chrome;
    const int density = 420;
    const int width = 1080;
    const int height = 1200;
    int scroll = 0;
    chrome_place(&chrome, width, height, 80, 600, density, 1, 1000, &scroll, NULL, 0);
    int thumb = thumb_of(density);
    expect(chrome.h[CHROME_OPEN] >= thumb, "a menu row stays a thumb when the keyboard is open");
    expect(chrome.h[CHROME_SAVE_AS] == chrome.h[CHROME_OPEN], "menu rows share one height");
    expect(chrome.y[CHROME_OPEN] >= 80, "open starts under the status inset");
    expect(chrome.y[CHROME_OPTIONS] + chrome.h[CHROME_OPTIONS] > height - 600,
           "the list is taller than the gap above the keyboard");
    scroll = 100000;
    chrome_place(&chrome, width, height, 80, 600, density, 1, 1000, &scroll, NULL, 0);
    expect(scroll > 0, "the menu scrolls");
    expect(chrome.y[CHROME_OPTIONS] + chrome.h[CHROME_OPTIONS] <= height - 600,
           "settings scrolls into the gap above the keyboard");
    expect(chrome_hit(&chrome, width / 2, chrome.y[CHROME_OPTIONS] + chrome.h[CHROME_OPTIONS] / 2) == CHROME_OPTIONS,
           "a scrolled row is the control under the finger");
    expect(chrome_label_px(160, 1000) == 24, "menu labels are 24sp");
    expect(chrome_label_px(160, 1300) == 31, "menu labels follow a 1.3 font scale");
    expect(chrome_label_px(460, 1300) == 89, "menu labels on a large phone are 89 pixels");
    expect(chrome_label_px(460, 1300) > text_px(460, 0), "menu labels are larger than the caption size");
    expect(chrome_row_px(160, 1000) >= 48, "a row is at least a thumb at mdpi");
    int ys[4];
    int hs[4];
    scroll = 0;
    expect(chrome_stack(ys, hs, 4, 10, 40, 20, &scroll) == 0, "a tall stack places");
    expect(ys[0] == 10 && hs[0] == 20, "the first row starts at the top");
    scroll = 1000;
    expect(chrome_stack(ys, hs, 4, 10, 40, 20, &scroll) == 0, "a tall stack scrolls");
    expect(scroll == 50, "scroll stops at the last row");
    expect(ys[3] + hs[3] == 40, "the last row meets the bottom");
    expect(chrome_stack(NULL, hs, 4, 0, 10, 8, NULL) == -1, "a null row list is refused");
    expect(chrome_stack(ys, hs, 257, 0, 10, 8, NULL) == -1, "a huge row list is refused");
}

static void test_text_px(void) {
    expect(text_step_count() == 7, "seven sizes");
    expect(text_step_default() == 4, "the default step is 24sp");
    int small = text_px(160, 0);
    int mid = text_px(160, text_step_default());
    int big = text_px(160, text_step_count() - 1);
    expect(small == 14 && mid == 24 && big == 32, "steps are 14, 24, and 32 pixels at mdpi");
    expect(text_px(320, 2) == 36, "18sp doubles at 320 dpi");
    expect(text_px(80, 2) == text_px(160, 2), "a tiny density uses 160");
    expect(text_px(320, -3) == text_px(320, 0), "a step below the first clamps");
    expect(text_px(320, 40) == text_px(320, 6), "a step past the last clamps");
}

int main(void) {
    test_phone();
    test_settings_view();
    test_wide();
    test_menu_size();
    test_text_px();
    if (g_failed) {
        return 1;
    }
    printf("chrome_test ok\n");
    return 0;
}
