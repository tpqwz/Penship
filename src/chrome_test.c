#include "chrome.h"

#include <stdio.h>
#include <stdlib.h>

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
    chrome_place(&chrome, width, height, 80, 160, density, 0);
    expect(chrome.vertical == 0, "the writing surface is not a column of options");
    expect(chrome.text_w == width && chrome.text_h == height - 80 - 160, "the page uses the space above the insets");
    expect_placed(&chrome, CHROME_SETTINGS, width, height);
    expect(chrome.w[CHROME_SMALLER] == 0 && chrome.w[CHROME_FONT] == 0, "options wait in the settings view");
    int thumb = thumb_of(density);
    expect(chrome.w[CHROME_SETTINGS] <= thumb * 2 && chrome.h[CHROME_SETTINGS] <= thumb * 2,
           "settings is a small floating button");
    expect(chrome.x[CHROME_SETTINGS] + chrome.w[CHROME_SETTINGS] > width - thumb, "the gear sits at the end");
    expect(chrome.y[CHROME_SETTINGS] <= 80 + thumb, "the gear sits under the status inset");
    expect(chrome.y[CHROME_SETTINGS] + chrome.h[CHROME_SETTINGS] <= height - 160, "the gear sits above the bottom inset");
    expect(chrome_hit(&chrome, 40, chrome.text_y + 40) == -1, "the page is not a control");
    expect(chrome_label(CHROME_SETTINGS)[0] == 'S', "settings label");
}

static void test_settings_view(void) {
    struct Chrome chrome;
    const int width = 1080;
    const int height = 2400;
    const int density = 420;
    chrome_place(&chrome, width, height, 80, 160, density, 1);
    expect(chrome.w[CHROME_SETTINGS] == 0, "the writing control is hidden in the settings view");
    int options[] = {CHROME_SMALLER, CHROME_LARGER, CHROME_FONT, CHROME_TYPEWRITER, CHROME_FOCUS, CHROME_THEME,
                     CHROME_SOURCE, CHROME_DONE};
    for (int i = 0; i < 8; i++) {
        expect_placed(&chrome, options[i], width, height);
        expect(chrome.y[options[i]] >= 80, "an option sits below the status inset");
        expect(chrome.y[options[i]] + chrome.h[options[i]] <= height - 160, "an option sits above the bottom inset");
    }
    expect(chrome.y[CHROME_DONE] > chrome.y[CHROME_SOURCE], "done sits after the options");
    expect(chrome_label(CHROME_FONT)[0] == 'F', "font label");
    expect(chrome_label(CHROME_DONE)[0] == 'D', "done label");
}

static void test_wide(void) {
    struct Chrome chrome;
    const int width = 2400;
    const int height = 1080;
    const int density = 320;
    chrome_place(&chrome, width, height, 40, 40, density, 0);
    expect(chrome.text_x == 0 && chrome.text_w == width, "a wide page uses the full width");
    expect(chrome.text_h == height - 40 - 40, "a wide page uses the space above the insets");
    expect_placed(&chrome, CHROME_SETTINGS, width, height);
    expect(chrome.w[CHROME_SETTINGS] < width / 4, "the gear does not become a bar");
    expect(chrome.y[CHROME_SETTINGS] <= 40 + thumb_of(density), "the wide gear sits under the status inset");
    expect(chrome.w[CHROME_THEME] == 0, "wide writing hides the options");
    expect(chrome.text_y == 40, "the page starts under the top inset");
}

static void test_text_px(void) {
    expect(text_step_count() == 7, "seven sizes");
    expect(text_step_default() == 2, "the default step is 18sp");
    int small = text_px(160, 0);
    int mid = text_px(160, text_step_default());
    int big = text_px(160, text_step_count() - 1);
    expect(small == 14 && mid == 18 && big == 32, "steps are 14, 18, and 32 pixels at mdpi");
    expect(text_px(320, 2) == 36, "18sp doubles at 320 dpi");
    expect(text_px(80, 2) == text_px(160, 2), "a tiny density uses 160");
    expect(text_px(320, -3) == text_px(320, 0), "a step below the first clamps");
    expect(text_px(320, 40) == text_px(320, 6), "a step past the last clamps");
}

int main(void) {
    test_phone();
    test_settings_view();
    test_wide();
    test_text_px();
    if (g_failed) {
        return 1;
    }
    printf("chrome_test ok\n");
    return 0;
}
