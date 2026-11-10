#include "chrome.h"
#include "doc.h"
#include "html.h"
#include "ime.h"
#include "page.h"
#include "utf8.h"

#include <android/asset_manager.h>
#include <android/configuration.h>
#include <android/input.h>
#include <android/keycodes.h>
#include <android/log.h>
#include <android_native_app_glue.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <jni.h>

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/system_properties.h>
#include <time.h>
#include <unistd.h>

#define LOG_TAG "penship"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

/* First buffer row is texture v = 0, and v = 0 sits at the top of the window. */
static const float QUAD[] = {
    -1.f, 1.f, 0.f, 0.f, 1.f, 1.f, 1.f, 0.f, -1.f, -1.f, 0.f, 1.f, 1.f, -1.f, 1.f, 1.f,
};

static const char *VERT =
    "attribute vec2 aPos;\n"
    "attribute vec2 aUv;\n"
    "varying vec2 vUv;\n"
    "void main() {\n"
    "  vUv = aUv;\n"
    "  gl_Position = vec4(aPos, 0.0, 1.0);\n"
    "}\n";

static const char *FRAG =
    "precision mediump float;\n"
    "varying vec2 vUv;\n"
    "uniform sampler2D uTex;\n"
    "void main() {\n"
    "  gl_FragColor = texture2D(uTex, vUv);\n"
    "}\n";

enum { SESSION_NONE = 0, SESSION_NOTE = 1, SESSION_URI = 2, SESSION_FAILED = 3 };

struct App {
    struct android_app *android;
    JNIEnv *env;
    Doc *doc;
    char path[1024];
    char copy_path[1024];
    char last_path[1024];
    char prefs_path[1024];
    char export_path[1024];
    char open_id[2048];
    int session;
    unsigned char *fonts[3];
    int font_lens[3];
    unsigned char *custom;
    int custom_len;
    char font_path[512];
    int glide;
    float glide_v;
    float glide_carry;
    float glide_frac;
    int glide_y[8];
    int64_t glide_ms[8];
    int glide_n;
    int64_t glide_at;
    int face;
    int step;
    int settings;
    int options;
    int menu_scroll;
    int contents;
    int contents_scroll;
    int contents_n;
    int contents_row_h;
    int bar_scroll;
    int bar_hit;
    int sel_menu;
    int sel_x;
    int sel_y;
    int sel_w;
    int sel_h;
    int sel_n;
    int tug;
    int contents_row_y[PAGE_HEADS_MAX + 2];
    size_t head_at[PAGE_HEADS_MAX];
    int head_n;
    int wheel;
    int wheel_scroll;
    int wheel_row_h;
    int wheel_band_y;
    int trail_x;
    int trail_y;
    int trail_w;
    int trail_h;
    int finding;
    char query[129];
    size_t query_n;
    size_t find_at;
    int compose_n;
    size_t compose_end;
    struct Chrome chrome;
    int font_px;
    EGLDisplay display;
    EGLSurface surface;
    EGLContext context;
    GLuint program;
    GLuint texture;
    int32_t width;
    int32_t height;
    uint8_t *pixels;
    int ready;
    int drag;
    int drag_x;
    int drag_y;
    int drag_last;
    int drag_mode;
    int64_t drag_at;
    int64_t tap_at;
    int tap_x;
    int tap_y;
    int tap_pending;
    size_t tap_off;
    int edge;
    int edge_x;
    int edge_y;
};

static GLuint compile_shader(GLenum type, const char *source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(shader, (GLsizei)sizeof log, NULL, log);
        LOGE("shader: %s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint link_program(void) {
    GLuint vert = compile_shader(GL_VERTEX_SHADER, VERT);
    GLuint frag = compile_shader(GL_FRAGMENT_SHADER, FRAG);
    if (vert == 0 || frag == 0) {
        glDeleteShader(vert);
        glDeleteShader(frag);
        return 0;
    }
    GLuint program = glCreateProgram();
    glAttachShader(program, vert);
    glAttachShader(program, frag);
    glBindAttribLocation(program, 0, "aPos");
    glBindAttribLocation(program, 1, "aUv");
    glLinkProgram(program);
    glDeleteShader(vert);
    glDeleteShader(frag);
    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(program, (GLsizei)sizeof log, NULL, log);
        LOGE("link: %s", log);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

static void stop_gl(struct App *app) {
    if (app->display == EGL_NO_DISPLAY) {
        return;
    }
    eglMakeCurrent(app->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (app->program != 0) {
        glDeleteProgram(app->program);
    }
    if (app->texture != 0) {
        glDeleteTextures(1, &app->texture);
    }
    if (app->context != EGL_NO_CONTEXT) {
        eglDestroyContext(app->display, app->context);
    }
    if (app->surface != EGL_NO_SURFACE) {
        eglDestroySurface(app->display, app->surface);
    }
    eglTerminate(app->display);
    app->display = EGL_NO_DISPLAY;
    app->context = EGL_NO_CONTEXT;
    app->surface = EGL_NO_SURFACE;
    app->program = 0;
    app->texture = 0;
    app->width = 0;
    app->height = 0;
    app->ready = 0;
    free(app->pixels);
    app->pixels = NULL;
}

static int start_gl(struct App *app) {
    if (app->android->window == NULL) {
        return 0;
    }
    stop_gl(app);

    const EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE,
    };
    const EGLint context_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, NULL, NULL)) {
        LOGE("eglInitialize failed");
        return 0;
    }
    EGLConfig config;
    EGLint count = 0;
    if (!eglChooseConfig(display, config_attribs, &config, 1, &count) || count < 1) {
        LOGE("eglChooseConfig failed");
        eglTerminate(display);
        return 0;
    }
    EGLint format = 0;
    eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &format);
    ANativeWindow_setBuffersGeometry(app->android->window, 0, 0, format);
    EGLSurface surface = eglCreateWindowSurface(display, config, app->android->window, NULL);
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attribs);
    if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
        !eglMakeCurrent(display, surface, surface, context)) {
        LOGE("egl surface failed");
        if (context != EGL_NO_CONTEXT) {
            eglDestroyContext(display, context);
        }
        if (surface != EGL_NO_SURFACE) {
            eglDestroySurface(display, surface);
        }
        eglTerminate(display);
        return 0;
    }

    app->display = display;
    app->surface = surface;
    app->context = context;
    app->program = link_program();
    glGenTextures(1, &app->texture);
    glBindTexture(GL_TEXTURE_2D, app->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glClearColor(PAGE_PAPER_R / 255.f, PAGE_PAPER_G / 255.f, PAGE_PAPER_B / 255.f, 1.f);
    app->ready = 1;
    return 1;
}

static void read_insets(struct App *app, int *top, int *bottom);
static int font_scale_milli(struct App *app);
static void hide_keyboard(struct App *app);
static void claim_back(struct App *app);

static void contents_show(struct App *app, uint8_t *px, int width, int height, int top, int bottom, int label_px,
                          int row_h) {
    struct PageHead heads[PAGE_HEADS_MAX];
    char shown[PAGE_HEADS_MAX][PAGE_HEAD_TITLE + 16];
    const char *labels[PAGE_HEADS_MAX + 2];
    int ys[PAGE_HEADS_MAX + 2];
    int hs[PAGE_HEADS_MAX + 2];
    int xs[PAGE_HEADS_MAX + 2];
    int ws[PAGE_HEADS_MAX + 2];
    size_t len = 0;
    const char *text = app->doc != NULL ? doc_text(app->doc, &len) : "";
    int nheads = page_heads(text, len, heads, PAGE_HEADS_MAX);
    if (nheads < 0) {
        nheads = 0;
    }
    if (nheads > PAGE_HEADS_MAX) {
        nheads = PAGE_HEADS_MAX;
    }
    app->head_n = nheads;
    for (int i = 0; i < nheads; i++) {
        app->head_at[i] = heads[i].at;
        int indent = heads[i].level - 1;
        if (indent < 0) {
            indent = 0;
        }
        if (indent > 5) {
            indent = 5;
        }
        indent *= 2;
        size_t o = 0;
        while (indent > 0 && o + 1 < sizeof shown[i]) {
            shown[i][o] = ' ';
            o++;
            indent--;
        }
        size_t t = 0;
        while (t < PAGE_HEAD_TITLE && heads[i].title[t] != '\0' && o + 1 < sizeof shown[i]) {
            shown[i][o] = heads[i].title[t];
            o++;
            t++;
        }
        shown[i][o] = '\0';
        labels[i + 1] = shown[i];
    }
    int rows = nheads > 0 ? nheads + 1 : 2;
    labels[0] = "Menu";
    if (nheads == 0) {
        labels[1] = "No headings";
    }
    if (row_h < 1) {
        row_h = 1;
    }
    if (chrome_stack(ys, hs, rows, top, bottom, row_h, &app->contents_scroll) != 0) {
        app->contents_n = 0;
        app->contents_row_h = row_h;
        return;
    }
    app->contents_n = rows;
    app->contents_row_h = row_h;
    for (int i = 0; i < rows; i++) {
        app->contents_row_y[i] = ys[i];
        xs[i] = 0;
        ws[i] = width > 0 ? width : 1;
    }
    page_draw_labels(px, width, height, rows, xs, ys, ws, hs, labels, 0, label_px, top, bottom);
}

static void fill_box(uint8_t *px, int width, int height, int x, int y, int w, int h, int r, int g, int b) {
    if (px == NULL || w < 1 || h < 1) {
        return;
    }
    for (int row = 0; row < h; row++) {
        int yy = y + row;
        if (yy < 0 || yy >= height) {
            continue;
        }
        for (int col = 0; col < w; col++) {
            int xx = x + col;
            if (xx < 0 || xx >= width) {
                continue;
            }
            uint8_t *p = px + ((size_t)yy * (size_t)width + (size_t)xx) * 4;
            p[0] = (uint8_t)r;
            p[1] = (uint8_t)g;
            p[2] = (uint8_t)b;
            p[3] = 255;
        }
    }
}

static int fill_trail(struct App *app, char *buf, size_t cap) {
    size_t len = 0;
    const char *text = app->doc != NULL ? doc_text(app->doc, &len) : "";
    struct PageHead trail[6];
    size_t at = page_view_ready() ? page_view_at() : doc_caret(app->doc);
    int n = 0;
    size_t o = 0;
    if (buf == NULL || cap < 2) {
        return 0;
    }
    buf[0] = '\0';
    if (at > len) {
        at = len;
    }
    n = page_trail(text, len, at, trail, 6);
    for (int i = 0; i < n && o + 1 < cap; i++) {
        size_t t = 0;
        if (i > 0 && o + 3 < cap) {
            buf[o++] = ' ';
            buf[o++] = '/';
            buf[o++] = ' ';
        }
        while (trail[i].title[t] != '\0' && o + 1 < cap) {
            buf[o++] = trail[i].title[t++];
        }
    }
    buf[o] = '\0';
    return o > 0 ? n : 0;
}

static int trail_hit(const struct App *app, int x, int y) {
    return app->trail_w > 0 && app->trail_h > 0 && x >= app->trail_x && y >= app->trail_y &&
           x < app->trail_x + app->trail_w && y < app->trail_y + app->trail_h;
}

static void open_wheel(struct App *app) {
    size_t len = 0;
    const char *text = app->doc != NULL ? doc_text(app->doc, &len) : "";
    struct PageHead heads[PAGE_HEADS_MAX];
    int n = page_heads(text, len, heads, PAGE_HEADS_MAX);
    size_t at = page_view_ready() ? page_view_at() : doc_caret(app->doc);
    int idx = 0;
    int density = 160;
    int row_h = 0;
    if (at > len) {
        at = len;
    }
    for (int i = 0; i < n; i++) {
        if (heads[i].at <= at) {
            idx = i;
        }
    }
    if (app->android != NULL && app->android->config != NULL) {
        density = AConfiguration_getDensity(app->android->config);
    }
    row_h = chrome_row_px(density, font_scale_milli(app));
    if (row_h < 1) {
        row_h = 1;
    }
    app->wheel = 1;
    app->wheel_scroll = n > 0 ? idx * row_h : 0;
    hide_keyboard(app);
    claim_back(app);
}

static void wheel_show(struct App *app, uint8_t *px, int width, int height, int top, int bottom, int label_px) {
    struct PageHead heads[PAGE_HEADS_MAX];
    char shown[PAGE_HEADS_MAX][PAGE_HEAD_TITLE + 8];
    const char *labels[PAGE_HEADS_MAX];
    int xs[PAGE_HEADS_MAX];
    int ys[PAGE_HEADS_MAX];
    int ws[PAGE_HEADS_MAX];
    int hs[PAGE_HEADS_MAX];
    size_t len = 0;
    const char *text = app->doc != NULL ? doc_text(app->doc, &len) : "";
    int n = page_heads(text, len, heads, PAGE_HEADS_MAX);
    int row_h = app->wheel_row_h;
    int span = 0;
    int band_y = 0;
    int max_scroll = 0;
    if (n < 0) {
        n = 0;
    }
    if (n > PAGE_HEADS_MAX) {
        n = PAGE_HEADS_MAX;
    }
    app->head_n = n;
    for (int i = 0; i < n; i++) {
        app->head_at[i] = heads[i].at;
    }
    if (row_h < 1) {
        row_h = label_px + label_px / 2;
    }
    if (row_h < 1) {
        row_h = 1;
    }
    app->wheel_row_h = row_h;
    span = bottom - top;
    if (span < row_h) {
        span = row_h;
    }
    band_y = top + span / 2 - row_h / 2;
    max_scroll = n > 1 ? (n - 1) * row_h : 0;
    if (app->wheel_scroll < 0) {
        app->wheel_scroll = 0;
    }
    if (app->wheel_scroll > max_scroll) {
        app->wheel_scroll = max_scroll;
    }
    app->wheel_band_y = band_y;
    fill_box(px, width, height, 0, top, width, bottom - top, PAGE_PAPER_R, PAGE_PAPER_G, PAGE_PAPER_B);
    fill_box(px, width, height, 0, band_y, width, row_h, PAGE_BLOCK_R, PAGE_BLOCK_G, PAGE_BLOCK_B);
    if (n == 0) {
        const char *empty = "No sections";
        int x = 0;
        int w = width > 0 ? width : 1;
        int h = row_h;
        page_draw_labels(px, width, height, 1, &x, &band_y, &w, &h, &empty, 0, label_px, top, bottom);
        return;
    }
    for (int i = 0; i < n; i++) {
        int indent = heads[i].level - 1;
        size_t o = 0;
        if (indent < 0) {
            indent = 0;
        }
        if (indent > 3) {
            indent = 3;
        }
        while (indent > 0 && o + 1 < sizeof shown[i]) {
            shown[i][o++] = ' ';
            indent--;
        }
        for (size_t t = 0; heads[i].title[t] != '\0' && o + 1 < sizeof shown[i]; t++) {
            shown[i][o++] = heads[i].title[t];
        }
        shown[i][o] = '\0';
        xs[i] = 0;
        ys[i] = band_y + i * row_h - app->wheel_scroll;
        ws[i] = width > 0 ? width : 1;
        hs[i] = row_h;
        /* A row that starts above the wheel would show only its feet. */
        labels[i] = ys[i] < top ? "" : shown[i];
    }
    page_draw_labels(px, width, height, n, xs, ys, ws, hs, labels, 0, label_px, top, bottom);
}

static void render(struct App *app) {
    if (!app->ready || app->doc == NULL) {
        return;
    }
    EGLint width = 0;
    EGLint height = 0;
    eglQuerySurface(app->display, app->surface, EGL_WIDTH, &width);
    eglQuerySurface(app->display, app->surface, EGL_HEIGHT, &height);
    if (width <= 0 || height <= 0) {
        return;
    }
    if (width != app->width || height != app->height || app->pixels == NULL) {
        uint8_t *pixels = malloc((size_t)width * (size_t)height * 4);
        if (pixels == NULL) {
            LOGE("page alloc %d x %d failed", width, height);
            return;
        }
        free(app->pixels);
        app->pixels = pixels;
        app->width = width;
        app->height = height;
    }
    int inset_top = 0;
    int inset_bottom = 0;
    read_insets(app, &inset_top, &inset_bottom);
    int density = 160;
    if (app->android != NULL && app->android->config != NULL) {
        density = AConfiguration_getDensity(app->android->config);
    }
    app->font_px = text_px(density, app->step);
    int scale = font_scale_milli(app);
    int label_px = chrome_label_px(density, scale);
    int screen = app->options ? 2 : (app->settings ? 1 : 0);
    chrome_place(&app->chrome, width, height, inset_top, inset_bottom, density, screen, scale,
                 screen != 0 ? &app->menu_scroll : NULL, screen == 0 ? &app->bar_scroll : NULL, page_editing());
    int find_x = 0;
    int find_y = 0;
    int find_w = 0;
    int find_h = 0;
    if (app->finding && !app->settings) {
        find_h = chrome_row_px(density, scale);
        find_x = app->chrome.text_x;
        find_y = app->chrome.text_y;
        find_w = app->chrome.text_w > 0 ? app->chrome.text_w : 1;
        if (app->chrome.text_h > find_h + 1) {
            app->chrome.text_y += find_h;
            app->chrome.text_h -= find_h;
        }
    }
    app->trail_w = 0;
    app->trail_h = 0;
    char trail_text[PAGE_HEAD_TITLE * 6 + 24];
    trail_text[0] = '\0';
    if (page_sections() && !app->settings && !app->finding) {
        int trail_n = fill_trail(app, trail_text, sizeof trail_text);
        if (trail_n > 0) {
            int gap = label_px / 3;
            int trail_h = label_px + label_px / 2;
            int side = 0;
            int mark_x = 0;
            if (gap < 8) {
                gap = 8;
            }
            if (trail_h < 8) {
                trail_h = 8;
            }
            /* The mark is the same height as the bar and shares its top. */
            side = trail_h;
            if (side > width) {
                side = width;
            }
            mark_x = width - side - gap;
            if (mark_x < 0) {
                mark_x = 0;
            }
            if (app->chrome.text_h > trail_h + 1) {
                app->chrome.x[CHROME_SETTINGS] = mark_x;
                app->chrome.y[CHROME_SETTINGS] = app->chrome.text_y;
                app->chrome.w[CHROME_SETTINGS] = side;
                app->chrome.h[CHROME_SETTINGS] = side;
                app->trail_x = app->chrome.text_x;
                app->trail_y = app->chrome.text_y;
                app->trail_h = trail_h;
                app->trail_w = mark_x - gap - app->trail_x;
                if (app->trail_w < 1) {
                    app->trail_w = 1;
                }
                app->chrome.text_y += trail_h;
                app->chrome.text_h -= trail_h;
            }
        }
    }
    size_t len = 0;
    const char *text = doc_text(app->doc, &len);
    /* Count before a settings frame, which paints an empty page and would report zero words. */
    page_draw(app->pixels, width, height, app->chrome.text_x, app->chrome.text_y, app->chrome.text_w,
              app->chrome.text_h, text, len, doc_caret(app->doc), doc_anchor(app->doc), app->font_px);
    if (page_sections() && !app->settings && !app->finding && app->trail_h > 0) {
        fill_trail(app, trail_text, sizeof trail_text);
    }
    int words = page_words();
    if (app->settings) {
        page_draw(app->pixels, width, height, 0, 0, 1, 1, "", 0, 0, 0, app->font_px);
    }
    const char *labels[CHROME_COUNT];
    for (int i = 0; i < CHROME_COUNT; i++) {
        labels[i] = (i == CHROME_SETTINGS || (i >= CHROME_BOLD && i <= CHROME_BLOCK)) ? "" : chrome_label(i);
    }
    static const char *k_glide_label[] = {"Glide off", "Glide short", "Glide medium", "Glide long", "Glide far"};
    {
        int level = page_glide_level();
        if (level < 0) {
            level = 0;
        }
        if (level > 4) {
            level = 4;
        }
        labels[CHROME_GLIDE] = k_glide_label[level];
    }
    labels[CHROME_SECTIONS] = page_sections() ? "Sections on" : "Sections off";
    {
        static const char *k_bar_label[] = {"Bar clear", "Bar wash", "Bar solid"};
        int level = page_bar();
        if (level < 0) {
            level = 0;
        }
        if (level > 2) {
            level = 2;
        }
        labels[CHROME_BAR] = k_bar_label[level];
    }
    int clip_bottom = height - inset_bottom;
    if (app->settings && app->contents) {
        contents_show(app, app->pixels, width, height, inset_top, clip_bottom, label_px, chrome_row_px(density, scale));
    } else {
        int mark_px = app->settings ? label_px : chrome_bar_px(density, scale);
        page_draw_labels(app->pixels, width, height, CHROME_COUNT, app->chrome.x, app->chrome.y, app->chrome.w,
                         app->chrome.h, labels, app->chrome.vertical, mark_px, inset_top, clip_bottom);
        if (page_bar_cover() > 0 && app->chrome.w[CHROME_BOLD] > 0 && app->chrome.h[CHROME_BOLD] > 0) {
            int x0 = app->chrome.x[CHROME_BOLD];
            int y0 = app->chrome.y[CHROME_BOLD];
            int x1 = x0;
            int y1 = y0 + app->chrome.h[CHROME_BOLD];
            for (int tool = CHROME_BOLD; tool <= CHROME_BLOCK; tool++) {
                if (app->chrome.w[tool] <= 0) {
                    continue;
                }
                if (app->chrome.x[tool] < x0) {
                    x0 = app->chrome.x[tool];
                }
                int right = app->chrome.x[tool] + app->chrome.w[tool];
                if (right > x1) {
                    x1 = right;
                }
            }
            int cover = page_bar_cover();
            if (cover >= 255) {
                fill_box(app->pixels, width, height, x0, y0, x1 - x0, y1 - y0, PAGE_PAPER_R, PAGE_PAPER_G, PAGE_PAPER_B);
            } else {
                for (int row = 0; row < y1 - y0; row++) {
                    int yy = y0 + row;
                    if (yy < 0 || yy >= height) {
                        continue;
                    }
                    for (int col = 0; col < x1 - x0; col++) {
                        int xx = x0 + col;
                        if (xx < 0 || xx >= width) {
                            continue;
                        }
                        uint8_t *p = app->pixels + ((size_t)yy * (size_t)width + (size_t)xx) * 4;
                        p[0] = (uint8_t)((PAGE_PAPER_R * cover + (int)p[0] * (255 - cover)) / 255);
                        p[1] = (uint8_t)((PAGE_PAPER_G * cover + (int)p[1] * (255 - cover)) / 255);
                        p[2] = (uint8_t)((PAGE_PAPER_B * cover + (int)p[2] * (255 - cover)) / 255);
                    }
                }
            }
        }
        for (int tool = CHROME_BOLD; tool <= CHROME_BLOCK; tool++) {
            if (app->chrome.w[tool] > 0 && app->chrome.h[tool] > 0) {
                page_draw_tool(app->pixels, width, height, app->chrome.x[tool], app->chrome.y[tool],
                               app->chrome.w[tool], app->chrome.h[tool], tool - CHROME_BOLD);
            }
        }
    }
    if (app->settings && !app->contents && app->chrome.h[CHROME_OPEN] > 0) {
        char stat[48];
        snprintf(stat, sizeof stat, words == 1 ? "1 word" : "%d words", words);
        const char *stat_label = stat;
        int stat_px = label_px;
        int stat_w = stat_px * 8;
        int row_w = app->chrome.w[CHROME_OPEN];
        if (stat_w > row_w / 2) {
            stat_w = row_w / 2;
        }
        int stat_x = app->chrome.x[CHROME_OPEN] + row_w - stat_w;
        int stat_y = app->chrome.y[CHROME_OPEN];
        int stat_h = app->chrome.h[CHROME_OPEN];
        page_draw_labels(app->pixels, width, height, 1, &stat_x, &stat_y, &stat_w, &stat_h, &stat_label, 0, stat_px,
                         inset_top, clip_bottom);
    }
    if (app->finding && !app->settings && find_h > 0) {
        const char *shown = app->query_n > 0 ? app->query : "Find";
        page_draw_labels(app->pixels, width, height, 1, &find_x, &find_y, &find_w, &find_h, &shown, 0, label_px,
                         inset_top, clip_bottom);
    }
    if (app->wheel && !app->settings) {
        int row_h = chrome_row_px(density, scale);
        int top = app->chrome.text_y;
        int bottom = height - inset_bottom;
        if (row_h < 1) {
            row_h = 1;
        }
        app->wheel_row_h = row_h;
        if (bottom < top) {
            bottom = top;
        }
        wheel_show(app, app->pixels, width, height, top, bottom, label_px);
    }
    if (app->trail_h > 0 && trail_text[0] != '\0') {
        const char *shown = trail_text;
        fill_box(app->pixels, width, height, app->trail_x, app->trail_y, app->trail_w, app->trail_h, PAGE_PAPER_R,
                 PAGE_PAPER_G, PAGE_PAPER_B);
        page_draw_labels(app->pixels, width, height, 1, &app->trail_x, &app->trail_y, &app->trail_w, &app->trail_h,
                         &shown, 0, label_px, inset_top, clip_bottom);
        fill_box(app->pixels, width, height, app->trail_x, app->trail_y + app->trail_h - 1, app->trail_w, 1,
                 PAGE_RULE_R, PAGE_RULE_G, PAGE_RULE_B);
    }
    page_draw_menu(app->pixels, width, height, app->chrome.x[CHROME_SETTINGS], app->chrome.y[CHROME_SETTINGS],
                   app->chrome.w[CHROME_SETTINGS]);
    app->sel_n = 0;
    if (app->sel_menu && doc_caret(app->doc) != doc_anchor(app->doc)) {
        int x0 = 0;
        int y0 = 0;
        int x1 = 0;
        int y1 = 0;
        int sh = 0;
        if (page_sel_ends(&x0, &y0, &x1, &y1, &sh) == 0) {
            int row = label_px + label_px / 2;
            if (row < 48) {
                row = 48;
            }
            int mw = label_px * 5;
            if (mw < row * 2) {
                mw = row * 2;
            }
            if (mw > width - 16) {
                mw = width - 16;
            }
            int mx = x0;
            if (mx < 8) {
                mx = 8;
            }
            if (mx > width - mw - 8) {
                mx = width - mw - 8;
            }
            int my = y0 - 8 - row * 3;
            int bar_top = app->chrome.h[CHROME_BOLD] > 0 ? app->chrome.y[CHROME_BOLD] : height;
            if (my < inset_top) {
                my = y0 + sh + 8;
            }
            if (my + row * 3 > bar_top && y0 - 8 - row * 3 >= inset_top) {
                my = y0 - 8 - row * 3;
            }
            app->sel_x = mx;
            app->sel_y = my;
            app->sel_w = mw;
            app->sel_h = row;
            app->sel_n = 3;
            int xs[3] = {mx, mx, mx};
            int ys[3] = {my, my + row, my + row * 2};
            int ws[3] = {mw, mw, mw};
            int hs[3] = {row, row, row};
            const char *items[3] = {"Edit", "Copy", "Cut"};
            page_draw_labels(app->pixels, width, height, 3, xs, ys, ws, hs, items, 0, label_px, inset_top, bar_top);
        }
    }
    glViewport(0, 0, width, height);
    glBindTexture(GL_TEXTURE_2D, app->texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, app->pixels);
    glClear(GL_COLOR_BUFFER_BIT);
    if (app->program != 0) {
        glUseProgram(app->program);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, app->texture);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), QUAD);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), QUAD + 2);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    eglSwapBuffers(app->display, app->surface);
}

static int write_uri(struct App *app);
static void pick_file(struct App *app, int create);
static void save_prefs(struct App *app);
static unsigned char *load_asset(AAssetManager *manager, const char *name, int *len);

static volatile int g_reload;
static volatile int g_font_pick;
static volatile int g_redraw;
static volatile int g_back;
static int g_keyboard_hold;

/* 0 when the open file was written. -1 when there is no writable file. */
static int save_doc(struct App *app) {
    if (app->doc == NULL) {
        return -1;
    }
    if (app->session == SESSION_URI) {
        if (write_uri(app) == 0) {
            return 0;
        }
        /* shortcut: a read-only open saves files/opened.md and is not merged back later. */
        LOGE("save to opened file failed");
        if (app->copy_path[0] != '\0' && doc_save(app->doc, app->copy_path) != 0) {
            LOGE("save copy failed");
        }
        return -1;
    }
    if (app->session == SESSION_NOTE && app->path[0] != '\0') {
        if (doc_save(app->doc, app->path) != 0) {
            LOGE("save failed");
            return -1;
        }
        return 0;
    }
    return -1;
}

static void save_prefs(struct App *app) {
    if (app->prefs_path[0] == '\0') {
        return;
    }
    int step = app->step;
    int last = text_step_count() - 1;
    if (step < 0) {
        step = 0;
    }
    if (step > last) {
        step = last;
    }
    int face = app->face;
    if (face < 0) {
        face = 0;
    }
    if (face > 3) {
        face = 0;
    }
    FILE *f = fopen(app->prefs_path, "w");
    if (f == NULL) {
        return;
    }
    if (fprintf(f,
                "step=%d\nface=%d\ntypewriter=%d\nfocus=%d\nline=%d\nglide=%d\ntheme=%d\nsource=%d\nreadonly=%d\n"
                "sections=%d\nbar=%d\n",
                step, face, page_typewriter(), page_focus(), page_line(), page_glide_level(), page_theme(),
                page_source(), page_readonly(), page_sections(), page_bar()) < 0) {
        fclose(f);
        return;
    }
    fclose(f);
}

static void load_prefs(struct App *app) {
    app->step = text_step_default();
    app->face = 0;
    if (app->prefs_path[0] == '\0') {
        return;
    }
    FILE *f = fopen(app->prefs_path, "r");
    if (f == NULL) {
        return;
    }
    char line[64];
    while (fgets(line, (int)sizeof line, f) != NULL) {
        int value = 0;
        if (sscanf(line, "step=%d", &value) == 1) {
            app->step = value;
        } else if (sscanf(line, "face=%d", &value) == 1) {
            app->face = value;
        } else if (sscanf(line, "typewriter=%d", &value) == 1) {
            page_set_typewriter(value);
        } else if (sscanf(line, "focus=%d", &value) == 1) {
            page_set_focus(value);
        } else if (sscanf(line, "line=%d", &value) == 1) {
            page_set_line(value);
        } else if (sscanf(line, "glide=%d", &value) == 1) {
            page_set_glide(value);
        } else if (sscanf(line, "theme=%d", &value) == 1) {
            page_set_theme(value);
        } else if (sscanf(line, "source=%d", &value) == 1) {
            page_set_source(value);
        } else if (sscanf(line, "readonly=%d", &value) == 1) {
            page_set_readonly(value);
        } else if (sscanf(line, "sections=%d", &value) == 1) {
            page_set_sections(value);
        } else if (sscanf(line, "bar=%d", &value) == 1) {
            page_set_bar(value);
        }
    }
    fclose(f);
    int last = text_step_count() - 1;
    if (app->step < 0) {
        app->step = 0;
    }
    if (app->step > last) {
        app->step = last;
    }
    if (app->face < 0) {
        app->face = 0;
    }
    if (app->face > 2) {
        app->face = 2;
    }
}

static int seed_sample(struct App *app) {
    if (app->android == NULL || app->android->activity == NULL || app->android->activity->assetManager == NULL) {
        return -1;
    }
    int n = 0;
    unsigned char *bytes = load_asset(app->android->activity->assetManager, "showcase.md", &n);
    if (bytes == NULL || n <= 0) {
        free(bytes);
        return -1;
    }
    int rc = doc_load_bytes(app->doc, (const char *)bytes, (size_t)n);
    free(bytes);
    if (rc != 0) {
        return -1;
    }
    if (app->path[0] != '\0' && doc_save(app->doc, app->path) != 0) {
        LOGE("sample save failed");
    }
    LOGI("seeded sample %d", n);
    return 0;
}

static JNIEnv *jni_env(struct App *app) {
    if (app->env != NULL) {
        return app->env;
    }
    JavaVM *vm = app->android->activity->vm;
    JNIEnv *env = NULL;
    if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK) {
        LOGE("attach failed");
        return NULL;
    }
    app->env = env;
    return env;
}

static int jni_ok(JNIEnv *env) {
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        return 0;
    }
    return 1;
}

static jclass load_class(struct App *app, JNIEnv *env, const char *class_name) {
    jobject activity = app->android->activity->clazz;
    if (activity == NULL) {
        return NULL;
    }
    jclass activity_class = (*env)->GetObjectClass(env, activity);
    jmethodID get_loader =
        activity_class != NULL ? (*env)->GetMethodID(env, activity_class, "getClassLoader", "()Ljava/lang/ClassLoader;") : NULL;
    jobject loader = NULL;
    if (jni_ok(env) && get_loader != NULL) {
        loader = (*env)->CallObjectMethod(env, activity, get_loader);
    }
    jclass found = NULL;
    if (jni_ok(env) && loader != NULL) {
        jclass loader_class = (*env)->GetObjectClass(env, loader);
        jmethodID load = (*env)->GetMethodID(env, loader_class, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
        jstring name = (*env)->NewStringUTF(env, class_name);
        if (jni_ok(env) && load != NULL && name != NULL) {
            found = (jclass)(*env)->CallObjectMethod(env, loader, load, name);
        }
        if (!jni_ok(env)) {
            found = NULL;
        }
        (*env)->DeleteLocalRef(env, name);
        (*env)->DeleteLocalRef(env, loader_class);
    }
    (*env)->DeleteLocalRef(env, loader);
    (*env)->DeleteLocalRef(env, activity_class);
    return found;
}

static void show_keyboard(struct App *app) {
    if (page_readonly() && !app->finding) {
        return;
    }
    g_keyboard_hold = 0;
    JNIEnv *env = jni_env(app);
    if (env == NULL || app->android->activity->clazz == NULL) {
        return;
    }
    jclass keyboard = load_class(app, env, "penship.Keyboard");
    if (keyboard == NULL) {
        LOGE("keyboard class missing");
        return;
    }
    jmethodID show = (*env)->GetStaticMethodID(env, keyboard, "show", "(Landroid/app/Activity;)V");
    if (jni_ok(env) && show != NULL) {
        (*env)->CallStaticVoidMethod(env, keyboard, show, app->android->activity->clazz);
        jni_ok(env);
    }
    (*env)->DeleteLocalRef(env, keyboard);
}

static void claim_back(struct App *app) {
    JNIEnv *env = jni_env(app);
    if (env == NULL || app->android == NULL || app->android->activity == NULL || app->android->activity->clazz == NULL) {
        return;
    }
    jclass pen = load_class(app, env, "penship.Pen");
    if (pen == NULL) {
        return;
    }
    jmethodID claim = (*env)->GetStaticMethodID(env, pen, "claimBack", "(Landroid/app/Activity;)V");
    if (jni_ok(env) && claim != NULL) {
        (*env)->CallStaticVoidMethod(env, pen, claim, app->android->activity->clazz);
        jni_ok(env);
    }
    (*env)->DeleteLocalRef(env, pen);
}

static void hide_keyboard(struct App *app) {
    JNIEnv *env = jni_env(app);
    if (env == NULL || app->android->activity->clazz == NULL) {
        return;
    }
    jclass keyboard = load_class(app, env, "penship.Keyboard");
    if (keyboard == NULL) {
        return;
    }
    jmethodID hide = (*env)->GetStaticMethodID(env, keyboard, "hide", "(Landroid/app/Activity;)V");
    if (jni_ok(env) && hide != NULL) {
        (*env)->CallStaticVoidMethod(env, keyboard, hide, app->android->activity->clazz);
        jni_ok(env);
    }
    (*env)->DeleteLocalRef(env, keyboard);
}

static int font_scale_milli(struct App *app) {
    JNIEnv *env = jni_env(app);
    if (env == NULL || app->android == NULL || app->android->activity == NULL || app->android->activity->clazz == NULL) {
        return 1000;
    }
    jclass keyboard = load_class(app, env, "penship.Keyboard");
    if (keyboard == NULL) {
        return 1000;
    }
    jmethodID scale = (*env)->GetStaticMethodID(env, keyboard, "fontScaleMilli", "(Landroid/app/Activity;)I");
    int milli = 1000;
    if (jni_ok(env) && scale != NULL) {
        milli = (*env)->CallStaticIntMethod(env, keyboard, scale, app->android->activity->clazz);
        if (!jni_ok(env) || milli < 500 || milli > 3000) {
            milli = 1000;
        }
    }
    (*env)->DeleteLocalRef(env, keyboard);
    return milli;
}

static void read_insets(struct App *app, int *top, int *bottom) {
    *top = 0;
    *bottom = 0;
    JNIEnv *env = jni_env(app);
    if (env == NULL || app->android->activity->clazz == NULL) {
        return;
    }
    jclass keyboard = load_class(app, env, "penship.Keyboard");
    if (keyboard == NULL) {
        return;
    }
    jmethodID insets = (*env)->GetStaticMethodID(env, keyboard, "insets", "(Landroid/app/Activity;)[I");
    jintArray values = NULL;
    if (jni_ok(env) && insets != NULL) {
        values = (jintArray)(*env)->CallStaticObjectMethod(env, keyboard, insets, app->android->activity->clazz);
    }
    if (jni_ok(env) && values != NULL && (*env)->GetArrayLength(env, values) >= 2) {
        jint pair[2];
        (*env)->GetIntArrayRegion(env, values, 0, 2, pair);
        *top = pair[0];
        *bottom = pair[1];
    }
    (*env)->DeleteLocalRef(env, values);
    (*env)->DeleteLocalRef(env, keyboard);
}

static void pick_file(struct App *app, int create) {
    JNIEnv *env = jni_env(app);
    if (env == NULL || app->android->activity->clazz == NULL) {
        return;
    }
    jclass pen = load_class(app, env, "penship.Pen");
    if (pen == NULL) {
        return;
    }
    jmethodID pick = (*env)->GetStaticMethodID(env, pen, "pick", "(Landroid/app/Activity;I)V");
    if (jni_ok(env) && pick != NULL) {
        (*env)->CallStaticVoidMethod(env, pen, pick, app->android->activity->clazz, create);
        if (!jni_ok(env)) {
            LOGE("pick failed");
        }
    }
    (*env)->DeleteLocalRef(env, pen);
}

/* 1 and the id is written, 0 when this launch is the scratch note, -1 on failure. */
static int copy_id(struct App *app, char *buf, size_t cap) {
    buf[0] = '\0';
    JNIEnv *env = jni_env(app);
    if (env == NULL || app->android->activity->clazz == NULL || cap == 0) {
        return -1;
    }
    jclass pen = load_class(app, env, "penship.Pen");
    if (pen == NULL) {
        return -1;
    }
    jmethodID id = (*env)->GetStaticMethodID(env, pen, "id", "(Landroid/app/Activity;)Ljava/lang/String;");
    jstring value = NULL;
    if (jni_ok(env) && id != NULL) {
        value = (jstring)(*env)->CallStaticObjectMethod(env, pen, id, app->android->activity->clazz);
    }
    int kind = -1;
    if (jni_ok(env)) {
        if (value == NULL) {
            kind = 0;
        } else {
            jsize n = (*env)->GetStringUTFLength(env, value);
            if (n >= 0 && (size_t)n < cap) {
                const char *chars = (*env)->GetStringUTFChars(env, value, NULL);
                if (chars != NULL) {
                    memcpy(buf, chars, (size_t)n);
                    buf[n] = '\0';
                    (*env)->ReleaseStringUTFChars(env, value, chars);
                    kind = 1;
                }
            }
        }
    }
    (*env)->DeleteLocalRef(env, value);
    (*env)->DeleteLocalRef(env, pen);
    return kind;
}

static int read_uri(struct App *app, char **out, size_t *out_n) {
    *out = NULL;
    *out_n = 0;
    JNIEnv *env = jni_env(app);
    if (env == NULL || app->android->activity->clazz == NULL) {
        return -1;
    }
    jclass pen = load_class(app, env, "penship.Pen");
    if (pen == NULL) {
        return -1;
    }
    jmethodID read = (*env)->GetStaticMethodID(env, pen, "read", "(Landroid/app/Activity;)[B");
    jbyteArray array = NULL;
    if (jni_ok(env) && read != NULL) {
        array = (jbyteArray)(*env)->CallStaticObjectMethod(env, pen, read, app->android->activity->clazz);
    }
    int rc = -1;
    if (jni_ok(env) && array != NULL) {
        jsize n = (*env)->GetArrayLength(env, array);
        if (n >= 0 && (size_t)n <= DOC_MAX_BYTES) {
            char *bytes = malloc((size_t)n + 1);
            if (bytes != NULL) {
                if (n > 0) {
                    (*env)->GetByteArrayRegion(env, array, 0, n, (jbyte *)bytes);
                }
                if (jni_ok(env)) {
                    bytes[n] = '\0';
                    *out = bytes;
                    *out_n = (size_t)n;
                    rc = 0;
                } else {
                    free(bytes);
                }
            }
        }
    }
    (*env)->DeleteLocalRef(env, array);
    (*env)->DeleteLocalRef(env, pen);
    return rc;
}

static int write_uri(struct App *app) {
    JNIEnv *env = jni_env(app);
    if (env == NULL || app->android->activity->clazz == NULL || app->doc == NULL) {
        return -1;
    }
    jclass pen = load_class(app, env, "penship.Pen");
    if (pen == NULL) {
        return -1;
    }
    jmethodID write = (*env)->GetStaticMethodID(env, pen, "write", "(Landroid/app/Activity;[B)I");
    size_t len = 0;
    const char *text = doc_text(app->doc, &len);
    jbyteArray array = NULL;
    int rc = -1;
    if (jni_ok(env) && write != NULL && len <= DOC_MAX_BYTES) {
        array = (*env)->NewByteArray(env, (jsize)len);
        if (jni_ok(env) && array != NULL && len > 0) {
            (*env)->SetByteArrayRegion(env, array, 0, (jsize)len, (const jbyte *)text);
        }
        if (jni_ok(env) && array != NULL) {
            rc = (*env)->CallStaticIntMethod(env, pen, write, app->android->activity->clazz, array);
            if (!jni_ok(env)) {
                rc = -1;
            }
        }
    }
    (*env)->DeleteLocalRef(env, array);
    (*env)->DeleteLocalRef(env, pen);
    return rc;
}

/* A file URI we opened ourselves. Rejects anything that is not an absolute path. */
static int file_uri_path(const char *id, char *out, size_t cap) {
    const char *prefix = "file://";
    size_t prefix_n = strlen(prefix);
    if (id == NULL || strncmp(id, prefix, prefix_n) != 0) {
        return -1;
    }
    const char *path = id + prefix_n;
    size_t n = strlen(path);
    if (path[0] != '/' || n == 0 || n >= cap || strstr(path, "/../") != NULL || strstr(path, "/./") != NULL) {
        return -1;
    }
    memcpy(out, path, n + 1);
    return 0;
}

static void remember_last(struct App *app, const char *id) {
    if (app->last_path[0] == '\0' || id == NULL || id[0] == '\0' || strlen(id) > 2000) {
        return;
    }
    FILE *f = fopen(app->last_path, "w");
    if (f == NULL) {
        return;
    }
    if (fputs(id, f) < 0 || fputc('\n', f) == EOF) {
        fclose(f);
        return;
    }
    fclose(f);
}

/* The launcher intent has no document. Reopen the file from the last session. */
static int reopen_last(struct App *app) {
    if (app->last_path[0] == '\0') {
        return -1;
    }
    FILE *f = fopen(app->last_path, "r");
    if (f == NULL) {
        return -1;
    }
    char id[2048];
    if (fgets(id, (int)sizeof id, f) == NULL) {
        fclose(f);
        return -1;
    }
    fclose(f);
    size_t n = strlen(id);
    if (n == 0 || id[n - 1] != '\n') {
        return -1;
    }
    id[n - 1] = '\0';
    char path[1024];
    if (file_uri_path(id, path, sizeof path) != 0 || doc_load(app->doc, path) != 0) {
        return -1;
    }
    memcpy(app->open_id, id, n);
    app->session = SESSION_URI;
    LOGI("reopened %s", id);
    return 0;
}

/* Load the opened document, or the scratch note when the launch has none. */
static int refresh_session(struct App *app) {
    char id[sizeof app->open_id];
    int kind = copy_id(app, id, sizeof id);
    if (kind < 0) {
        LOGE("intent failed");
        if (app->session != SESSION_NONE) {
            return -1;
        }
        kind = 0;
    }
    if (kind == 1) {
        if ((app->session == SESSION_URI || app->session == SESSION_FAILED) && strcmp(id, app->open_id) == 0) {
            return 0;
        }
        char *bytes = NULL;
        size_t n = 0;
        if (read_uri(app, &bytes, &n) != 0 || doc_load_bytes(app->doc, bytes, n) != 0) {
            free(bytes);
            doc_load_bytes(app->doc, "", 0);
            memcpy(app->open_id, id, strlen(id) + 1);
            app->session = SESSION_FAILED;
            LOGE("open failed");
            return 1;
        }
        free(bytes);
        memcpy(app->open_id, id, strlen(id) + 1);
        app->session = SESSION_URI;
        remember_last(app, id);
        LOGI("opened %zu bytes", n);
        return 1;
    }
    /* No document on this resume. Keep the note already open. */
    if (app->session != SESSION_NONE) {
        return kind < 0 ? -1 : 0;
    }
    if (reopen_last(app) == 0) {
        return 1;
    }
    app->open_id[0] = '\0';
    int loaded = app->path[0] != '\0' && doc_load(app->doc, app->path) == 0;
    size_t len = 0;
    if (loaded) {
        doc_text(app->doc, &len);
    }
    if (!loaded || len == 0) {
        if (seed_sample(app) != 0) {
            doc_load_bytes(app->doc, "", 0);
        }
    }
    app->session = SESSION_NOTE;
    return 1;
}

/* shortcut: KeyEvent.getUnicodeChar on the virtual keyboard. Dead keys are dropped. */
static int unicode_of(struct App *app, int key_code, int meta) {
    JNIEnv *env = jni_env(app);
    if (env == NULL) {
        return 0;
    }
    jclass cls = (*env)->FindClass(env, "android/view/KeyEvent");
    if (!jni_ok(env) || cls == NULL) {
        return 0;
    }
    jmethodID ctor = (*env)->GetMethodID(env, cls, "<init>", "(II)V");
    jmethodID get = (*env)->GetMethodID(env, cls, "getUnicodeChar", "(I)I");
    if (!jni_ok(env) || ctor == NULL || get == NULL) {
        (*env)->DeleteLocalRef(env, cls);
        return 0;
    }
    jobject event = (*env)->NewObject(env, cls, ctor, AKEY_EVENT_ACTION_DOWN, key_code);
    int ch = 0;
    if (jni_ok(env) && event != NULL) {
        ch = (*env)->CallIntMethod(env, event, get, meta);
        if (!jni_ok(env)) {
            ch = 0;
        }
    }
    if ((ch & 0x80000000) != 0) {
        ch = 0;
    }
    (*env)->DeleteLocalRef(env, event);
    (*env)->DeleteLocalRef(env, cls);
    return ch;
}

enum { LOOPER_ID_IME = 4 };

static int g_ime_fd[2] = {-1, -1};

static void ime_wake(void) {
    if (g_ime_fd[1] < 0) {
        return;
    }
    char byte = 1;
    if (write(g_ime_fd[1], &byte, 1) < 0) {
        /* The glue thread drains the pipe. A full pipe means it is already awake. */
    }
}

static int codepoints_of(const char *bytes, size_t n) {
    int count = 0;
    size_t i = 0;
    while (i < n) {
        unsigned cp = 0;
        int k = utf8_decode(bytes + i, n - i, &cp);
        if (k < 0) {
            break;
        }
        i += (size_t)k;
        count++;
    }
    return count;
}

/* Drop the open composition. Returns 1 when bytes were removed. */
static int clear_composition(struct App *app) {
    int cleared = 0;
    if (app->compose_n > 0 && doc_anchor(app->doc) == doc_caret(app->doc) &&
        doc_caret(app->doc) == app->compose_end) {
        doc_move(app->doc, 0);
        doc_extend(app->doc, -app->compose_n);
        if (doc_backspace(app->doc) == 0) {
            cleared = 1;
        }
    }
    app->compose_n = 0;
    return cleared;
}

enum { CLIP_MAX = 1 << 20 };

static void selection_bounds(const Doc *doc, size_t *lo, size_t *hi) {
    size_t anchor = doc_anchor(doc);
    size_t caret = doc_caret(doc);
    *lo = anchor < caret ? anchor : caret;
    *hi = anchor < caret ? caret : anchor;
}

static int copy_selection(struct App *app) {
    size_t lo = 0;
    size_t hi = 0;
    selection_bounds(app->doc, &lo, &hi);
    if (lo == hi || hi - lo > CLIP_MAX || app->android->activity->clazz == NULL) {
        return 0;
    }
    JNIEnv *env = jni_env(app);
    if (env == NULL) {
        return 0;
    }
    jclass pen = load_class(app, env, "penship.Pen");
    if (pen == NULL) {
        return 0;
    }
    jmethodID set = (*env)->GetStaticMethodID(env, pen, "setClipboard", "(Landroid/app/Activity;[B)V");
    jbyteArray array = NULL;
    if (jni_ok(env) && set != NULL && hi - lo <= (size_t)0x7fffffff) {
        array = (*env)->NewByteArray(env, (jsize)(hi - lo));
    }
    int copied = 0;
    if (jni_ok(env) && array != NULL) {
        const char *text = doc_text(app->doc, NULL);
        (*env)->SetByteArrayRegion(env, array, 0, (jsize)(hi - lo), (const jbyte *)(text + lo));
        if (jni_ok(env)) {
            (*env)->CallStaticVoidMethod(env, pen, set, app->android->activity->clazz, array);
            copied = jni_ok(env);
        }
    }
    (*env)->DeleteLocalRef(env, array);
    (*env)->DeleteLocalRef(env, pen);
    return copied;
}

static int paste_clip(struct App *app) {
    if (app->android->activity->clazz == NULL) {
        return 0;
    }
    JNIEnv *env = jni_env(app);
    if (env == NULL) {
        return 0;
    }
    jclass pen = load_class(app, env, "penship.Pen");
    if (pen == NULL) {
        return 0;
    }
    jmethodID get = (*env)->GetStaticMethodID(env, pen, "getClipboard", "(Landroid/app/Activity;)[B");
    jbyteArray array = NULL;
    if (jni_ok(env) && get != NULL) {
        array = (jbyteArray)(*env)->CallStaticObjectMethod(env, pen, get, app->android->activity->clazz);
    }
    int changed = 0;
    if (jni_ok(env) && array != NULL) {
        jsize n = (*env)->GetArrayLength(env, array);
        if (jni_ok(env) && n > 0 && n <= CLIP_MAX) {
            jbyte *bytes = (*env)->GetByteArrayElements(env, array, NULL);
            if (bytes != NULL) {
                changed = doc_insert(app->doc, (const char *)bytes, (size_t)n) == 0;
                (*env)->ReleaseByteArrayElements(env, array, bytes, JNI_ABORT);
            }
        }
    }
    if (!jni_ok(env)) {
        changed = 0;
    }
    (*env)->DeleteLocalRef(env, array);
    (*env)->DeleteLocalRef(env, pen);
    return changed;
}

static int find_type(struct App *app, const char *bytes, size_t n);
static void find_pop(struct App *app);

static void apply_ime(struct App *app) {
    if (app->doc == NULL) {
        return;
    }
    struct ImeOp op;
    int changed = 0;
    while (ime_take(&op) == 0) {
        if (op.kind == IME_FINISH) {
            app->compose_n = 0;
        } else if (op.kind == IME_UNDO) {
            app->compose_n = 0;
            if (!page_readonly() && doc_undo(app->doc) == 0) {
                changed = 1;
            }
        } else if (op.kind == IME_SHORTCUT) {
            if (op.count == AKEYCODE_T) {
                page_set_typewriter(!page_typewriter());
            } else if (op.count == AKEYCODE_F) {
                page_set_focus(!page_focus());
            } else if (op.count == AKEYCODE_P) {
                page_set_theme(page_theme() == 0 ? 1 : 0);
            } else if (op.count == AKEYCODE_S) {
                page_set_source(!page_source());
            }
            render(app);
        } else if (op.kind == IME_SELECT) {
            app->compose_n = 0;
            doc_select_all(app->doc);
            page_set_follow(1);
            changed = 1;
        } else if (op.kind == IME_COPY) {
            copy_selection(app);
        } else if (op.kind == IME_CUT) {
            app->compose_n = 0;
            if (!page_readonly() && copy_selection(app) && doc_backspace(app->doc) == 0) {
                changed = 1;
            }
        } else if (op.kind == IME_PASTE) {
            app->compose_n = 0;
            if (!page_readonly() && paste_clip(app)) {
                changed = 1;
            }
        } else if (op.kind == IME_TAB && !page_readonly()) {
            int rc = doc_table_tab(app->doc, op.count);
            if (rc >= 0) {
                app->compose_n = 0;
                page_set_follow(1);
                if (rc > 0) {
                    save_doc(app);
                }
                render(app);
            }
        } else if (op.kind == IME_HEADING) {
            size_t len = 0;
            const char *text = doc_text(app->doc, &len);
            size_t at = page_heading(text, len, doc_caret(app->doc), op.count);
            doc_place(app->doc, at);
            app->compose_n = 0;
            page_set_follow(1);
            render(app);
        } else if ((op.kind == IME_ERASE || op.kind == IME_FORWARD) && !page_readonly()) {
            app->compose_n = 0;
            for (int i = 0; i < op.count; i++) {
                int rc = op.kind == IME_FORWARD ? doc_delete(app->doc) : doc_backspace(app->doc);
                if (rc != 0) {
                    break;
                }
                changed = 1;
            }
        } else if (op.kind == IME_MOVE || op.kind == IME_ROW) {
            page_set_follow(1);
            if (op.composing) {
                if (op.kind == IME_ROW) {
                    doc_extend_row(app->doc, op.count);
                } else {
                    doc_extend(app->doc, op.count);
                }
            } else if (op.kind == IME_ROW) {
                doc_move_row(app->doc, op.count);
            } else {
                doc_move(app->doc, op.count);
            }
            render(app);
        } else if (app->finding && op.kind == IME_TEXT) {
            find_type(app, op.bytes, op.n);
        } else if (app->finding && (op.kind == IME_ERASE || op.kind == IME_FORWARD)) {
            app->compose_n = 0;
            find_pop(app);
        } else if (op.kind == IME_TEXT && !page_readonly()) {
            if (clear_composition(app)) {
                changed = 1;
            }
            if (op.n > 0 && doc_insert(app->doc, op.bytes, op.n) == 0) {
                changed = 1;
                if (op.composing) {
                    app->compose_n = codepoints_of(op.bytes, op.n);
                    app->compose_end = doc_caret(app->doc);
                }
            }
        }
        free(op.bytes);
    }
    if (changed) {
        save_doc(app);
        page_set_follow(1);
        render(app);
        size_t len = 0;
        doc_text(app->doc, &len);
        LOGI("ime len=%zu caret=%zu", len, doc_caret(app->doc));
    }
}

JNIEXPORT void JNICALL Java_penship_Pen_backPressed(JNIEnv *env, jclass cls) {
    (void)env;
    (void)cls;
    g_back = 1;
    ime_wake();
}

JNIEXPORT void JNICALL Java_penship_Keyboard_wake(JNIEnv *env, jclass cls) {
    (void)env;
    (void)cls;
    g_redraw = 1;
    ime_wake();
}

JNIEXPORT void JNICALL Java_penship_Pen_fontPicked(JNIEnv *env, jclass cls) {
    (void)env;
    (void)cls;
    g_font_pick = 1;
    ime_wake();
}

JNIEXPORT void JNICALL Java_penship_Pen_reload(JNIEnv *env, jclass cls, jint adopt) {
    (void)env;
    (void)cls;
    g_reload = adopt ? 2 : 1;
    ime_wake();
}

JNIEXPORT void JNICALL Java_penship_Pen_commitBytes(JNIEnv *env, jclass cls, jbyteArray array, jint composing) {
    (void)cls;
    if (array == NULL) {
        return;
    }
    jsize n = (*env)->GetArrayLength(env, array);
    if (n < 0 || n > IME_TEXT_MAX) {
        return;
    }
    jbyte *bytes = n == 0 ? NULL : (*env)->GetByteArrayElements(env, array, NULL);
    if (n > 0 && bytes == NULL) {
        return;
    }
    int rc = ime_push_text(n == 0 ? "" : (const char *)bytes, (size_t)n, composing);
    if (bytes != NULL) {
        (*env)->ReleaseByteArrayElements(env, array, bytes, JNI_ABORT);
    }
    if (rc == 0) {
        ime_wake();
    }
}

JNIEXPORT void JNICALL Java_penship_Pen_erase(JNIEnv *env, jclass cls, jint count) {
    (void)env;
    (void)cls;
    if (ime_push_erase(count) == 0) {
        ime_wake();
    }
}

JNIEXPORT void JNICALL Java_penship_Pen_eraseForward(JNIEnv *env, jclass cls, jint count) {
    (void)env;
    (void)cls;
    if (ime_push_forward(count) == 0) {
        ime_wake();
    }
}

JNIEXPORT void JNICALL Java_penship_Pen_motion(JNIEnv *env, jclass cls, jint dir, jint rows, jint extend) {
    (void)env;
    (void)cls;
    int rc = rows ? ime_push_row(dir, extend) : ime_push_move(dir, extend);
    if (rc == 0) {
        ime_wake();
    }
}

JNIEXPORT void JNICALL Java_penship_Pen_finishComposing(JNIEnv *env, jclass cls) {
    (void)env;
    (void)cls;
    if (ime_push_finish() == 0) {
        ime_wake();
    }
}

JNIEXPORT void JNICALL Java_penship_Pen_undo(JNIEnv *env, jclass cls) {
    (void)env;
    (void)cls;
    if (ime_push_undo() == 0) {
        ime_wake();
    }
}

JNIEXPORT void JNICALL Java_penship_Pen_shortcut(JNIEnv *env, jclass cls, jint code) {
    (void)env;
    (void)cls;
    if (code != AKEYCODE_T && code != AKEYCODE_F && code != AKEYCODE_P && code != AKEYCODE_S) {
        return;
    }
    if (ime_push_shortcut(code) == 0) {
        ime_wake();
    }
}

JNIEXPORT void JNICALL Java_penship_Pen_tableTab(JNIEnv *env, jclass cls, jint dir) {
    (void)env;
    (void)cls;
    if (ime_push_tab(dir) == 0) {
        ime_wake();
    }
}

JNIEXPORT void JNICALL Java_penship_Pen_heading(JNIEnv *env, jclass cls, jint dir) {
    (void)env;
    (void)cls;
    if (ime_push_heading(dir) == 0) {
        ime_wake();
    }
}

JNIEXPORT void JNICALL Java_penship_Pen_clip(JNIEnv *env, jclass cls, jint kind) {
    (void)env;
    (void)cls;
    if (ime_push_clip(kind) == 0) {
        ime_wake();
    }
}

enum { HOLD_MS = 400, TAP_MS = 300 };

static int64_t now_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void select_word(struct App *app, size_t at) {
    size_t len = 0;
    const char *text = doc_text(app->doc, &len);
    size_t lo = 0;
    size_t hi = 0;
    if (doc_word_at(text, len, at, &lo, &hi) == 0) {
        doc_place(app->doc, lo);
        doc_seek(app->doc, hi);
    } else {
        doc_place(app->doc, at);
        app->sel_menu = 0;
    }
}

/* Move one end of the selection. end is 0 for the start and 1 for the finish. */
static void move_edge(struct App *app, int end, size_t at) {
    size_t caret = doc_caret(app->doc);
    size_t anchor = doc_anchor(app->doc);
    size_t lo = caret < anchor ? caret : anchor;
    size_t hi = caret < anchor ? anchor : caret;
    if (end) {
        if (caret >= anchor) {
            doc_seek(app->doc, at);
        } else {
            doc_place(app->doc, lo);
            doc_seek(app->doc, at);
        }
    } else if (caret <= anchor) {
        doc_seek(app->doc, at);
    } else {
        doc_place(app->doc, hi);
        doc_seek(app->doc, at);
    }
}

static int sel_row(const struct App *app, int x, int y) {
    if (app->sel_menu == 0 || app->sel_n <= 0 || app->sel_h < 1 || app->sel_w < 1) {
        return -1;
    }
    if (x < app->sel_x || x >= app->sel_x + app->sel_w || y < app->sel_y) {
        return -1;
    }
    int row = (y - app->sel_y) / app->sel_h;
    if (row < 0 || row >= app->sel_n || y >= app->sel_y + (row + 1) * app->sel_h) {
        return -1;
    }
    return row;
}

/* A held press selects the word under the finger. A later drag grows that selection. */
static void arm_select(struct App *app) {
    size_t len = 0;
    const char *text = doc_text(app->doc, &len);
    size_t at = page_hit(app->chrome.text_x, app->chrome.text_y, app->chrome.text_w, app->chrome.text_h, text, len,
                         doc_caret(app->doc), app->font_px, app->drag_x, app->drag_y);
    select_word(app, at);
    app->compose_n = 0;
    app->sel_menu = 0;
    app->drag_mode = 2;
    page_set_follow(0);
    render(app);
}

enum { FIND_MAX = 128 };

static void find_run(struct App *app, int next) {
    size_t len = 0;
    const char *text = doc_text(app->doc, &len);
    if (text == NULL) {
        text = "";
        len = 0;
    }
    if (app->query_n > 0) {
        size_t from = next ? doc_caret(app->doc) : app->find_at;
        if (from > len) {
            from = 0;
        }
        size_t at = page_find(text, len, app->query, app->query_n, from, 1);
        if (at < len && app->query_n <= len - at) {
            doc_place(app->doc, at);
            doc_seek(app->doc, at + app->query_n);
            app->find_at = at;
            page_set_follow(1);
        }
    }
    render(app);
}

static int find_type(struct App *app, const char *bytes, size_t n) {
    if (bytes == NULL || n == 0) {
        return 0;
    }
    for (size_t i = 0; i < n; i++) {
        if (bytes[i] == '\n') {
            find_run(app, 1);
            return 1;
        }
    }
    if (!utf8_valid(bytes, n) || n > FIND_MAX || app->query_n > FIND_MAX - n) {
        return 0;
    }
    memcpy(app->query + app->query_n, bytes, n);
    app->query_n += n;
    app->query[app->query_n] = '\0';
    find_run(app, 0);
    return 1;
}

static void find_pop(struct App *app) {
    if (app->query_n == 0) {
        render(app);
        return;
    }
    size_t i = app->query_n;
    do {
        i--;
    } while (i > 0 && ((unsigned char)app->query[i] & 0xc0) == 0x80);
    app->query_n = i;
    app->query[i] = '\0';
    find_run(app, 0);
}

/* The selection menu, then Contents, then the options menu, then Find. Back never leaves the activity. */
static void handle_back(struct App *app) {
    LOGE("back sel=%d contents=%d menu=%d find=%d", app->sel_menu, app->contents, app->settings, app->finding);
    if (app->sel_menu) {
        app->sel_menu = 0;
        app->sel_n = 0;
        render(app);
        return;
    }
    if (app->wheel) {
        app->wheel = 0;
        render(app);
        return;
    }
    if (app->contents) {
        app->contents = 0;
        app->contents_scroll = 0;
        render(app);
        return;
    }
    if (app->options) {
        app->options = 0;
        app->menu_scroll = 0;
        render(app);
        return;
    }
    if (app->settings) {
        app->settings = 0;
        app->options = 0;
        app->contents = 0;
        render(app);
        return;
    }
    if (app->finding) {
        app->finding = 0;
        app->query_n = 0;
        app->query[0] = '\0';
        render(app);
        return;
    }
    g_keyboard_hold = 1;
    hide_keyboard(app);
}

static void glide_note(struct App *app, int y) {
    int64_t t = now_ms();
    if (app->glide_n == 8) {
        memmove(app->glide_y, app->glide_y + 1, 7 * sizeof app->glide_y[0]);
        memmove(app->glide_ms, app->glide_ms + 1, 7 * sizeof app->glide_ms[0]);
        app->glide_n = 7;
    }
    app->glide_y[app->glide_n] = y;
    app->glide_ms[app->glide_n] = t;
    app->glide_n++;
}

/* Pixels per millisecond. The fast part of the flick counts, not the slow lift at the end. */
static float glide_speed(const struct App *app) {
    if (app->glide_n < 2) {
        return 0.f;
    }
    int last = app->glide_n - 1;
    int64_t end = app->glide_ms[last];
    int first = last;
    while (first > 0 && end - app->glide_ms[first - 1] <= 120) {
        first--;
    }
    float peak = 0.f;
    for (int i = first; i < last; i++) {
        int64_t step = app->glide_ms[i + 1] - app->glide_ms[i];
        if (step < 4) {
            continue;
        }
        float v = (float)(app->glide_y[i] - app->glide_y[i + 1]) / (float)step;
        float abs_v = v < 0.f ? -v : v;
        float abs_peak = peak < 0.f ? -peak : peak;
        if (abs_v > abs_peak) {
            peak = v;
        }
    }
    int64_t dt = end - app->glide_ms[first];
    float window = 0.f;
    if (dt >= 8) {
        window = (float)(app->glide_y[first] - app->glide_y[last]) / (float)dt;
    }
    float abs_peak = peak < 0.f ? -peak : peak;
    float abs_window = window < 0.f ? -window : window;
    int same_way = peak == 0.f || window == 0.f || (peak > 0.f) == (window > 0.f);
    if (same_way && abs_peak >= abs_window) {
        return peak;
    }
    return window;
}

static float screen_dpi(const struct App *app) {
    float dpi = 160.f;
    if (app->android != NULL && app->android->config != NULL) {
        int density = AConfiguration_getDensity(app->android->config);
        if (density >= 120 && density <= 1000) {
            dpi = (float)density;
        }
    }
    return dpi;
}

/* The emulator reports a phone density while the mouse moves in host pixels.
   A wheel tick on a long page is a stack of dips, so a drag here is judged in dips. */
static int emulator_pointer(void) {
    static int known = 0;
    static int yes = 0;
    char value[PROP_VALUE_MAX];
    int n = 0;
    if (known) {
        return yes;
    }
    value[0] = '\0';
    n = __system_property_get("ro.kernel.qemu", value);
    if (n > 0 && value[0] == '1' && value[1] == '\0') {
        yes = 1;
    }
    if (!yes) {
        value[0] = '\0';
        n = __system_property_get("ro.boot.qemu", value);
        if (n > 0 && value[0] == '1' && value[1] == '\0') {
            yes = 1;
        }
    }
    known = 1;
    return yes;
}

static int on_input(struct android_app *android, AInputEvent *event) {
    struct App *app = android->userData;
    if (app->doc == NULL) {
        return 0;
    }
    int type = AInputEvent_getType(event);
    if (type == AINPUT_EVENT_TYPE_MOTION) {
        int action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
        int x = (int)AMotionEvent_getX(event, 0);
        int y = (int)AMotionEvent_getY(event, 0);
        if (action == AMOTION_EVENT_ACTION_SCROLL) {
            float ticks = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_VSCROLL, 0);
            if (ticks > -0.01f && ticks < 0.01f) {
                return 1;
            }
            float dpi = screen_dpi(app);
            float pixels = ticks * 64.f * dpi / 160.f;
            if (pixels > 4000.f) {
                pixels = 4000.f;
            }
            if (pixels < -4000.f) {
                pixels = -4000.f;
            }
            /* A positive tick moves the content down, toward the start of the note. */
            int dy = (int)(-pixels);
            page_fling_cancel();
            app->glide = 0;
            app->glide_carry = 0.f;
            if (app->settings) {
                int *scroll = app->contents ? &app->contents_scroll : &app->menu_scroll;
                *scroll += dy;
                render(app);
                return 1;
            }
            int rows = page_center_drag(dy);
            if (rows != 0) {
                int step = rows > 0 ? 1 : -1;
                int n = rows > 0 ? rows : -rows;
                for (int i = 0; i < n && i < 8; i++) {
                    doc_move_row(app->doc, step);
                }
                app->compose_n = 0;
            }
            render(app);
            return 1;
        }
        if (action == AMOTION_EVENT_ACTION_DOWN) {
            if (app->wheel && !app->settings) {
                int down_hit = chrome_hit(&app->chrome, x, y);
                app->glide = 0;
                app->glide_n = 0;
                app->edge = 0;
                app->bar_hit = -1;
                if (down_hit == CHROME_SETTINGS || trail_hit(app, x, y)) {
                    app->wheel = down_hit == CHROME_SETTINGS ? 0 : app->wheel;
                    app->drag = 0;
                    return 0;
                }
                app->drag = 1;
                app->drag_x = x;
                app->drag_y = y;
                app->drag_last = y;
                app->drag_mode = 6;
                app->drag_at = now_ms();
                return 1;
            }
            /* Pause the slide, but keep its speed so a flick in the same direction can add to it. */
            app->glide = 0;
            app->glide_n = 0;
            if (app->settings) {
                app->glide_v = 0.f;
                app->drag = 1;
                app->drag_x = x;
                app->drag_y = y;
                app->drag_last = y;
                app->drag_mode = 0;
                app->drag_at = now_ms();
                app->edge = 0;
                return 1;
            }
            int down_hit = chrome_hit(&app->chrome, x, y);
            if (down_hit >= CHROME_BOLD && down_hit < CHROME_COUNT) {
                app->glide_v = 0.f;
                app->drag = 1;
                app->bar_hit = down_hit;
                app->drag_x = x;
                app->drag_y = y;
                app->drag_last = x;
                app->drag_mode = 0;
                app->drag_at = now_ms();
                app->edge = 0;
                return 1;
            }
            app->bar_hit = -1;
            if (down_hit >= 0) {
                app->glide_v = 0.f;
                app->drag = 0;
                return 0;
            }
            if (sel_row(app, x, y) >= 0) {
                app->glide_v = 0.f;
                app->drag = 0;
                return 0;
            }
            if (!app->settings && app->chrome.text_h > 0 &&
                x >= app->chrome.text_x + app->chrome.text_w - 28 && x < app->chrome.text_x + app->chrome.text_w &&
                y >= app->chrome.text_y && y < app->chrome.text_y + app->chrome.text_h) {
                app->glide_v = 0.f;
                app->glide_carry = 0.f;
                app->glide = 0;
                page_fling_cancel();
                app->drag = 1;
                app->drag_x = x;
                app->drag_y = y;
                app->drag_last = y;
                app->drag_mode = 5;
                app->drag_at = now_ms();
                app->edge = 0;
                float span = (float)app->chrome.text_h;
                float frac = span > 1.f ? (float)(y - app->chrome.text_y) / span : 0.f;
                page_scroll_place(frac);
                render(app);
                return 1;
            }
            int edge = page_sel_edge(x, y, 48);
            if (edge >= 0 && doc_caret(app->doc) != doc_anchor(app->doc)) {
                app->glide_v = 0.f;
                app->drag = 1;
                app->drag_x = x;
                app->drag_y = y;
                app->drag_last = y;
                app->drag_mode = 4;
                app->tug = edge;
                app->sel_menu = 0;
                app->drag_at = now_ms();
                app->edge = 0;
                return 1;
            }
            app->drag = 1;
            app->drag_x = x;
            app->drag_y = y;
            app->drag_last = y;
            app->drag_mode = 0;
            app->drag_at = now_ms();
            app->glide_carry = page_fling_velocity((int)(now_ms() - app->glide_at));
            app->glide = 0;
            app->glide_n = 0;
            app->edge = 0;
            return 1;
        }
        if (action == AMOTION_EVENT_ACTION_CANCEL) {
            app->drag = 0;
            app->glide = 0;
            app->glide_v = 0.f;
            app->edge = 0;
            app->bar_hit = -1;
            return 1;
        }
        if (action == AMOTION_EVENT_ACTION_MOVE && app->drag) {
            if (app->drag_mode == 6) {
                app->wheel_scroll += app->drag_last - y;
                app->drag_last = y;
                render(app);
                return 1;
            }
            if (app->bar_hit >= CHROME_BOLD) {
                int dx = x - app->drag_x;
                int dy = y - app->drag_y;
                if (dx < 0) {
                    dx = -dx;
                }
                if (dy < 0) {
                    dy = -dy;
                }
                if (app->drag_mode == 0 && dx > dy && dx > 36) {
                    app->drag_mode = 3;
                }
                if (app->drag_mode == 3) {
                    app->bar_scroll += app->drag_last - x;
                    app->drag_last = x;
                    render(app);
                }
                return 1;
            }
            if (app->drag_mode == 5) {
                float span = (float)app->chrome.text_h;
                float frac = span > 1.f ? (float)(y - app->chrome.text_y) / span : 0.f;
                page_scroll_place(frac);
                render(app);
                return 1;
            }
            if (app->drag_mode == 4) {
                size_t len = 0;
                const char *text = doc_text(app->doc, &len);
                size_t at = page_hit(app->chrome.text_x, app->chrome.text_y, app->chrome.text_w, app->chrome.text_h,
                                     text, len, doc_caret(app->doc), app->font_px, x, y);
                move_edge(app, app->tug, at);
                app->compose_n = 0;
                render(app);
                return 1;
            }
            if (app->settings) {
                int dx = x - app->drag_x;
                int dy = y - app->drag_y;
                if (dx < 0) {
                    dx = -dx;
                }
                if (dy < 0) {
                    dy = -dy;
                }
                if (app->drag_mode == 0 && (dx > 36 || dy > 36)) {
                    app->drag_mode = 1;
                }
                if (app->drag_mode == 1) {
                    int *scroll = app->contents ? &app->contents_scroll : &app->menu_scroll;
                    *scroll += app->drag_last - y;
                    app->drag_last = y;
                    render(app);
                }
                return 1;
            }
            int dx = x - app->drag_x;
            int dy = y - app->drag_y;
            if (dx < 0) {
                dx = -dx;
            }
            if (dy < 0) {
                dy = -dy;
            }
            /* A quick drag scrolls, unless it starts on the caret. A held press selects. */
            if (app->drag_mode == 0 && (dx > 36 || dy > 36)) {
                int held = now_ms() - app->drag_at >= HOLD_MS;
                int near = page_caret_near(app->drag_x, app->drag_y, 72);
                int kind = page_drag_kind(1, held, near, page_typewriter());
                if (kind == 2) {
                    arm_select(app);
                } else {
                    app->drag_mode = kind;
                }
            }
            if (app->drag_mode == 1) {
                int delta = app->drag_last - y;
                glide_note(app, y);
                int rows = page_center_drag(delta);
                app->drag_last = y;
                if (rows != 0) {
                    int step = rows > 0 ? 1 : -1;
                    int n = rows > 0 ? rows : -rows;
                    for (int i = 0; i < n; i++) {
                        doc_move_row(app->doc, step);
                    }
                    app->compose_n = 0;
                }
                render(app);
            } else if (app->drag_mode == 2) {
                size_t len = 0;
                const char *text = doc_text(app->doc, &len);
                doc_seek(app->doc, page_hit(app->chrome.text_x, app->chrome.text_y, app->chrome.text_w,
                                            app->chrome.text_h, text, len, doc_caret(app->doc), app->font_px, x, y));
                app->compose_n = 0;
                app->edge = page_edge(y, app->chrome.text_y, app->chrome.text_y + app->chrome.text_h, 72);
                app->edge_x = x;
                app->edge_y = y;
                render(app);
            }
            return 1;
        }
        if (action == AMOTION_EVENT_ACTION_UP) {
            int dragged = app->drag && app->drag_mode != 0;
            if (app->drag_mode == 6) {
                int row_h = app->wheel_row_h > 0 ? app->wheel_row_h : 1;
                int moved = y - app->drag_y;
                int moved_x = x - app->drag_x;
                app->drag = 0;
                app->drag_mode = 0;
                app->edge = 0;
                if (moved < 0) {
                    moved = -moved;
                }
                if (moved_x < 0) {
                    moved_x = -moved_x;
                }
                if (moved < 24 && moved_x < 24 && app->wheel && app->head_n > 0) {
                    int origin = app->wheel_band_y - app->wheel_scroll;
                    int row = -1;
                    if (y >= origin) {
                        row = (y - origin) / row_h;
                    }
                    if (row >= 0 && row < app->head_n && row < PAGE_HEADS_MAX) {
                        doc_place(app->doc, app->head_at[row]);
                        page_set_follow(1);
                    }
                    app->wheel = 0;
                } else {
                    int s = app->wheel_scroll;
                    int mod = s % row_h;
                    if (mod < 0) {
                        mod += row_h;
                    }
                    if (mod * 2 >= row_h) {
                        s += row_h - mod;
                    } else {
                        s -= mod;
                    }
                    app->wheel_scroll = s;
                }
                render(app);
                return 1;
            }
            int scrolling = app->drag_mode == 1;
            app->bar_hit = -1;
            int selecting = app->drag_mode == 2;
            app->drag = 0;
            app->edge = 0;
            if (scrolling) {
                glide_note(app, y);
                float flick = glide_speed(app) * 1000.f * page_glide_gain();
                if (flick > 200.f || flick < -200.f) {
                    float dpi = screen_dpi(app);
                    if (emulator_pointer() && dpi > 160.f) {
                        flick *= dpi / 160.f;
                    }
                    float v = app->glide_carry + flick;
                    if (v > 30000.f) {
                        v = 30000.f;
                    }
                    if (v < -30000.f) {
                        v = -30000.f;
                    }
                    if (page_fling_arm(v, dpi)) {
                        app->glide = 1;
                        app->glide_frac = 0.f;
                        app->glide_at = now_ms();
                    } else {
                        app->glide = 0;
                    }
                } else {
                    page_fling_cancel();
                    app->glide = 0;
                    app->glide_carry = 0.f;
                }
            } else {
                page_fling_cancel();
                app->glide = 0;
                app->glide_carry = 0.f;
            }
            if (dragged) {
                app->tap_pending = 0;
                app->tap_at = 0;
                if (selecting && doc_caret(app->doc) != doc_anchor(app->doc)) {
                    app->sel_menu = 1;
                    claim_back(app);
                    render(app);
                } else if (app->drag_mode == 4 && doc_caret(app->doc) != doc_anchor(app->doc)) {
                    app->sel_menu = 1;
                    claim_back(app);
                    render(app);
                }
                return 1;
            }
            int chosen = sel_row(app, x, y);
            if (app->sel_menu && chosen < 0) {
                int mark_hit = chrome_hit(&app->chrome, x, y);
                app->sel_menu = 0;
                if (mark_hit < CHROME_BOLD || mark_hit > CHROME_BLOCK) {
                    app->tap_pending = 0;
                    app->tap_at = 0;
                    render(app);
                    return 1;
                }
            }
            if (chosen >= 0) {
                app->tap_pending = 0;
                app->tap_at = 0;
                if (chosen == 0) {
                    page_set_readonly(0);
                    app->sel_menu = 0;
                    render(app);
                    show_keyboard(app);
                } else if (chosen == 1) {
                    copy_selection(app);
                    app->sel_menu = 0;
                    render(app);
                } else if (!page_readonly() && copy_selection(app) && doc_backspace(app->doc) == 0) {
                    app->sel_menu = 0;
                    save_doc(app);
                    render(app);
                }
                return 1;
            }
            if (app->settings && app->contents) {
                app->tap_pending = 0;
                app->tap_at = 0;
                int row = -1;
                int n = app->contents_n;
                if (n < 0) {
                    n = 0;
                }
                if (n > PAGE_HEADS_MAX + 2) {
                    n = PAGE_HEADS_MAX + 2;
                }
                int row_h = app->contents_row_h > 0 ? app->contents_row_h : 1;
                for (int i = 0; i < n; i++) {
                    int y0 = app->contents_row_y[i];
                    if (y >= y0 && y < y0 + row_h) {
                        row = i;
                        break;
                    }
                }
                if (row < 0) {
                    app->settings = 0;
                    app->options = 0;
                    app->contents = 0;
                    app->menu_scroll = 0;
                } else if (row == 0) {
                    app->contents = 0;
                    app->contents_scroll = 0;
                } else if (row > 0 && app->head_n > 0 && row - 1 < app->head_n && row - 1 < PAGE_HEADS_MAX) {
                    doc_place(app->doc, app->head_at[row - 1]);
                    page_set_follow(1);
                    app->settings = 0;
                    app->contents = 0;
                }
                render(app);
                return 1;
            }
            if (!dragged && trail_hit(app, x, y)) {
                app->tap_pending = 0;
                app->tap_at = 0;
                if (app->wheel) {
                    app->wheel = 0;
                } else {
                    open_wheel(app);
                }
                render(app);
                return 1;
            }
            int hit = chrome_hit(&app->chrome, x, y);
            if (hit >= 0) {
                app->tap_pending = 0;
                app->tap_at = 0;
            }
            if (hit == CHROME_SETTINGS) {
                app->wheel = 0;
                if (app->settings) {
                    app->settings = 0;
                    app->options = 0;
                    app->contents = 0;
                    app->menu_scroll = 0;
                } else {
                    app->settings = 1;
                    app->options = 0;
                    app->contents = 0;
                    app->menu_scroll = 0;
                    hide_keyboard(app);
                    claim_back(app);
                }
            } else if (hit == CHROME_OPTIONS) {
                app->options = 1;
                app->menu_scroll = 0;
                claim_back(app);
            } else if (hit == CHROME_OPEN) {
                app->settings = 0;
                app->contents = 0;
                pick_file(app, 0);
            } else if (hit == CHROME_SAVE) {
                int wrote = save_doc(app);
                app->settings = 0;
                app->contents = 0;
                /* The scratch note is private. A refused write needs a place the user picks. */
                if (wrote != 0 || app->session != SESSION_URI) {
                    pick_file(app, 1);
                }
            } else if (hit == CHROME_SAVE_AS) {
                app->settings = 0;
                app->contents = 0;
                pick_file(app, 1);
            } else if (hit == CHROME_FIND) {
                app->settings = 0;
                app->contents = 0;
                app->finding = 1;
                app->query_n = 0;
                app->query[0] = '\0';
                app->find_at = doc_caret(app->doc);
                claim_back(app);
                render(app);
                show_keyboard(app);
                return 1;
            } else if (hit == CHROME_CONTENTS) {
                app->contents = 1;
                app->contents_scroll = 0;
                hide_keyboard(app);
                claim_back(app);
            } else if (hit == CHROME_EXPORT) {
                save_doc(app);
                app->settings = 0;
                app->contents = 0;
                size_t len = 0;
                const char *text = doc_text(app->doc, &len);
                char *html = NULL;
                size_t html_n = 0;
                if (html_export(text, len, &html, &html_n) == 0 && app->export_path[0] != '\0') {
                    FILE *out = fopen(app->export_path, "wb");
                    if (out != NULL) {
                        size_t wrote = fwrite(html, 1, html_n, out);
                        int closed = fclose(out);
                        if (wrote == html_n && closed == 0) {
                            pick_file(app, 2);
                        }
                    }
                }
                free(html);
            } else if (app->settings && hit < 0) {
                app->settings = 0;
                app->options = 0;
                app->contents = 0;
                app->menu_scroll = 0;
                render(app);
                return 1;
            } else if (hit == CHROME_SMALLER && app->step > 0) {
                app->step--;
            } else if (hit == CHROME_LARGER && app->step + 1 < text_step_count()) {
                app->step++;
            } else if (hit == CHROME_FONT) {
                int faces = app->custom != NULL ? 4 : 3;
                for (int n = 0; n < faces; n++) {
                    app->face = (app->face + 1) % faces;
                    if (app->face == 3 && app->custom != NULL && page_set_font(app->custom, app->custom_len) == 0) {
                        break;
                    }
                    if (app->face < 3 && app->fonts[app->face] != NULL &&
                        page_set_font(app->fonts[app->face], app->font_lens[app->face]) == 0) {
                        break;
                    }
                }
            } else if (hit == CHROME_FONTFILE) {
                pick_file(app, 3);
            } else if (hit == CHROME_TYPEWRITER) {
                page_set_typewriter(!page_typewriter());
            } else if (hit == CHROME_FOCUS) {
                page_set_focus(!page_focus());
            } else if (hit == CHROME_LINE) {
                page_set_line(!page_line());
            } else if (hit == CHROME_SECTIONS) {
                page_set_sections(!page_sections());
                if (!page_sections()) {
                    app->wheel = 0;
                }
            } else if (hit == CHROME_BAR) {
                page_set_bar((page_bar() + 1) % 3);
            } else if (hit == CHROME_GLIDE) {
                page_set_glide((page_glide_level() + 1) % 5);
            } else if (hit == CHROME_THEME) {
                page_set_theme(page_theme() == 0 ? 1 : 0);
            } else if (hit == CHROME_SOURCE) {
                page_set_source(!page_source());
            } else if (hit == CHROME_READONLY) {
                page_set_readonly(!page_readonly());
                if (page_readonly()) {
                    hide_keyboard(app);
                }
            } else if (!page_readonly() && hit >= CHROME_BOLD && hit <= CHROME_BLOCK) {
                if (hit == CHROME_BOLD || hit == CHROME_ITALIC || hit == CHROME_STRIKE || hit == CHROME_CODE ||
                    hit == CHROME_LINK) {
                    doc_select_word(app->doc);
                }
                if (hit == CHROME_BOLD) {
                    doc_mark(app->doc, "**", 2);
                } else if (hit == CHROME_ITALIC) {
                    doc_mark(app->doc, "*", 1);
                } else if (hit == CHROME_STRIKE) {
                    doc_mark(app->doc, "~~", 2);
                } else if (hit == CHROME_CODE) {
                    doc_mark(app->doc, "`", 1);
                } else if (hit == CHROME_LINK) {
                    doc_link(app->doc);
                } else if (hit == CHROME_HEAD) {
                    doc_line(app->doc, 1);
                } else if (hit == CHROME_LIST) {
                    doc_line(app->doc, 2);
                } else if (hit == CHROME_TASK) {
                    doc_line(app->doc, 3);
                } else if (hit == CHROME_QUOTE) {
                    doc_line(app->doc, 4);
                } else if (hit == CHROME_RULE) {
                    doc_block(app->doc, 1);
                } else if (hit == CHROME_TABLE) {
                    doc_block(app->doc, 2);
                } else if (hit == CHROME_BLOCK) {
                    doc_block(app->doc, 3);
                }
                app->compose_n = 0;
                app->bar_hit = -1;
                page_set_follow(1);
                save_doc(app);
                render(app);
                show_keyboard(app);
                return 1;
            } else {
                size_t len = 0;
                const char *text = doc_text(app->doc, &len);
                size_t at = page_hit(app->chrome.text_x, app->chrome.text_y, app->chrome.text_w, app->chrome.text_h,
                                     text, len, doc_caret(app->doc), app->font_px, x, y);
                int64_t t = now_ms();
                int ddx = x - app->tap_x;
                int ddy = y - app->tap_y;
                if (ddx < 0) {
                    ddx = -ddx;
                }
                if (ddy < 0) {
                    ddy = -ddy;
                }
                int doubled = app->tap_at != 0 && t >= app->tap_at && t - app->tap_at <= TAP_MS && ddx < 48 && ddy < 48;
                app->tap_x = x;
                app->tap_y = y;
                app->tap_at = t;
                app->tap_pending = 0;
                if (doubled) {
                    size_t wlo = 0;
                    size_t whi = 0;
                    if (doc_word_at(text, len, at, &wlo, &whi) == 0) {
                        doc_place(app->doc, wlo);
                        doc_seek(app->doc, whi);
                        app->sel_menu = 1;
                        claim_back(app);
                    } else {
                        doc_place(app->doc, at);
                        app->sel_menu = 0;
                    }
                } else {
                    app->sel_menu = 0;
                    doc_place(app->doc, at);
                    app->tap_pending = 1;
                    app->tap_off = doc_caret(app->doc);
                }
                app->compose_n = 0;
                /* A tap stays on this page. Dragging is what scrolls. */
                page_set_follow(0);
                render(app);
                if (app->sel_menu == 0) {
                    show_keyboard(app);
                }
                return 1;
            }
            save_prefs(app);
            render(app);
            return 1;
        }
        return 0;
    }
    if (type != AINPUT_EVENT_TYPE_KEY || AKeyEvent_getAction(event) != AKEY_EVENT_ACTION_DOWN) {
        return 0;
    }
    int code = AKeyEvent_getKeyCode(event);
    int meta = AKeyEvent_getMetaState(event);
    int shift = (meta & (AMETA_SHIFT_ON | AMETA_SHIFT_LEFT_ON | AMETA_SHIFT_RIGHT_ON)) != 0;
    int ctrl = (meta & (AMETA_CTRL_ON | AMETA_CTRL_LEFT_ON | AMETA_CTRL_RIGHT_ON)) != 0;
    if (code == AKEYCODE_BACK) {
        handle_back(app);
        return 1;
    }
    if (ctrl && shift &&
        (code == AKEYCODE_T || code == AKEYCODE_F || code == AKEYCODE_P || code == AKEYCODE_S)) {
        if (code == AKEYCODE_T) {
            page_set_typewriter(!page_typewriter());
        } else if (code == AKEYCODE_F) {
            page_set_focus(!page_focus());
        } else if (code == AKEYCODE_S) {
            page_set_source(!page_source());
        } else {
            page_set_theme(page_theme() == 0 ? 1 : 0);
        }
        save_prefs(app);
        render(app);
        return 1;
    }
    if (ctrl && !shift && code == AKEYCODE_A) {
        app->compose_n = 0;
        doc_select_all(app->doc);
        page_set_follow(1);
        render(app);
        return 1;
    }
    if (ctrl && !shift && code == AKEYCODE_C) {
        copy_selection(app);
        return 1;
    }
    if (ctrl && !shift && code == AKEYCODE_X) {
        app->compose_n = 0;
        if (!page_readonly() && copy_selection(app) && doc_backspace(app->doc) == 0) {
            save_doc(app);
            page_set_follow(1);
            render(app);
        }
        return 1;
    }
    if (ctrl && !shift && code == AKEYCODE_V) {
        app->compose_n = 0;
        if (!page_readonly() && paste_clip(app)) {
            save_doc(app);
            page_set_follow(1);
            render(app);
        }
        return 1;
    }
    if (ctrl && code == AKEYCODE_Z) {
        if (!page_readonly() && doc_undo(app->doc) == 0) {
            save_doc(app);
            page_set_follow(1);
            render(app);
        }
        return 1;
    }
    if (code == AKEYCODE_TAB) {
        if (page_readonly()) {
            return 1;
        }
        int rc = doc_table_tab(app->doc, shift ? -1 : 1);
        if (rc >= 0) {
            app->compose_n = 0;
            page_set_follow(1);
            if (rc > 0) {
                save_doc(app);
            }
            render(app);
        }
        return 1;
    }
    if (app->finding && (code == AKEYCODE_ENTER || code == AKEYCODE_NUMPAD_ENTER)) {
        find_run(app, 1);
        return 1;
    }
    if (code == AKEYCODE_DEL || code == AKEYCODE_FORWARD_DEL) {
        app->compose_n = 0;
        if (app->finding) {
            find_pop(app);
            return 1;
        }
        if (page_readonly()) {
            return 1;
        }
        int rc = code == AKEYCODE_FORWARD_DEL ? doc_delete(app->doc) : doc_backspace(app->doc);
        if (rc == 0) {
            save_doc(app);
            page_set_follow(1);
            render(app);
        }
        return 1;
    }
    if (code == AKEYCODE_SYSTEM_NAVIGATION_UP) {
        code = AKEYCODE_DPAD_UP;
    } else if (code == AKEYCODE_SYSTEM_NAVIGATION_DOWN) {
        code = AKEYCODE_DPAD_DOWN;
    } else if (code == AKEYCODE_SYSTEM_NAVIGATION_LEFT) {
        code = AKEYCODE_DPAD_LEFT;
    } else if (code == AKEYCODE_SYSTEM_NAVIGATION_RIGHT) {
        code = AKEYCODE_DPAD_RIGHT;
    }
    if (ctrl && !shift && (code == AKEYCODE_DPAD_UP || code == AKEYCODE_DPAD_DOWN)) {
        size_t len = 0;
        const char *text = doc_text(app->doc, &len);
        size_t at = page_heading(text, len, doc_caret(app->doc), code == AKEYCODE_DPAD_DOWN ? 1 : -1);
        doc_place(app->doc, at);
        app->compose_n = 0;
        page_set_follow(1);
        render(app);
        return 1;
    }
    if (code == AKEYCODE_DPAD_LEFT) {
        if (shift) {
            doc_extend(app->doc, -1);
        } else {
            doc_move(app->doc, -1);
        }
        page_set_follow(1);
        render(app);
        return 1;
    }
    if (code == AKEYCODE_DPAD_RIGHT) {
        if (shift) {
            doc_extend(app->doc, 1);
        } else {
            doc_move(app->doc, 1);
        }
        page_set_follow(1);
        render(app);
        return 1;
    }
    if (code == AKEYCODE_DPAD_UP) {
        if (shift) {
            doc_extend_row(app->doc, -1);
        } else {
            doc_move_row(app->doc, -1);
        }
        page_set_follow(1);
        render(app);
        return 1;
    }
    if (code == AKEYCODE_DPAD_DOWN) {
        if (shift) {
            doc_extend_row(app->doc, 1);
        } else {
            doc_move_row(app->doc, 1);
        }
        page_set_follow(1);
        render(app);
        return 1;
    }
    int ch = unicode_of(app, code, AKeyEvent_getMetaState(event));
    if (ch <= 0 || (unsigned)ch > 0x10ffff) {
        return 0;
    }
    if (app->finding) {
        char bytes[4];
        int n = utf8_encode((unsigned)ch, bytes);
        if (n > 0) {
            find_type(app, bytes, (size_t)n);
        }
        return 1;
    }
    if (page_readonly()) {
        return 1;
    }
    app->compose_n = 0;
    char bytes[4];
    int n = utf8_encode((unsigned)ch, bytes);
    if (n < 0 || doc_insert(app->doc, bytes, (size_t)n) != 0) {
        return 1;
    }
    save_doc(app);
    page_set_follow(1);
    render(app);
    size_t len = 0;
    doc_text(app->doc, &len);
    LOGI("edit len=%zu caret=%zu", len, doc_caret(app->doc));
    return 1;
}

static void on_cmd(struct android_app *android, int32_t cmd) {
    struct App *app = android->userData;
    switch (cmd) {
    case APP_CMD_INIT_WINDOW:
        if (start_gl(app)) {
            render(app);
        }
        break;
    case APP_CMD_TERM_WINDOW:
        stop_gl(app);
        break;
    case APP_CMD_GAINED_FOCUS:
        /* A back that closed the keyboard must not open it again on the focus that follows. */
        if (!g_keyboard_hold) {
            show_keyboard(app);
        }
        render(app);
        break;
    case APP_CMD_PAUSE:
        save_doc(app);
        save_prefs(app);
        break;
    case APP_CMD_RESUME:
        refresh_session(app);
        render(app);
        break;
    case APP_CMD_WINDOW_RESIZED:
    case APP_CMD_WINDOW_REDRAW_NEEDED:
    case APP_CMD_CONTENT_RECT_CHANGED:
    case APP_CMD_CONFIG_CHANGED:
        render(app);
        break;
    default:
        break;
    }
}

static unsigned char *load_asset(AAssetManager *manager, const char *name, int *len) {
    AAsset *asset = AAssetManager_open(manager, name, AASSET_MODE_BUFFER);
    if (asset == NULL) {
        LOGE("missing asset %s", name);
        return NULL;
    }
    int n = (int)AAsset_getLength(asset);
    unsigned char *bytes = n > 0 ? malloc((size_t)n) : NULL;
    int got = bytes != NULL ? AAsset_read(asset, bytes, (size_t)n) : -1;
    AAsset_close(asset);
    if (got != n) {
        free(bytes);
        return NULL;
    }
    *len = n;
    return bytes;
}

enum { FONT_MAX = 8 * 1024 * 1024 };

/* 0 when the file is a TrueType the painter can read. The previous face stays put on failure. */
static int apply_custom_font(struct App *app) {
    if (app->font_path[0] == '\0') {
        return -1;
    }
    FILE *f = fopen(app->font_path, "rb");
    if (f == NULL) {
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    long sz = ftell(f);
    if (sz <= 0 || sz > FONT_MAX || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    unsigned char *buf = malloc((size_t)sz);
    if (buf == NULL) {
        fclose(f);
        return -1;
    }
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (n != (size_t)sz || page_set_font(buf, (int)n) != 0) {
        free(buf);
        return -1;
    }
    free(app->custom);
    app->custom = buf;
    app->custom_len = (int)n;
    app->face = 3;
    return 0;
}

void android_main(struct android_app *android) {
    struct App app;
    memset(&app, 0, sizeof app);
    app.bar_hit = -1;
    app.android = android;
    app.display = EGL_NO_DISPLAY;
    app.surface = EGL_NO_SURFACE;
    app.context = EGL_NO_CONTEXT;
    app.doc = doc_new();
    if (app.doc == NULL) {
        LOGE("doc_new failed");
        return;
    }
    app.step = text_step_default();
    int density = 160;
    if (android->config != NULL) {
        density = AConfiguration_getDensity(android->config);
    }
    app.font_px = text_px(density, app.step);
    const char *dir = android->activity->internalDataPath;
    if (dir != NULL) {
        int wrote = snprintf(app.path, sizeof app.path, "%s/note.md", dir);
        if (wrote < 0 || (size_t)wrote >= sizeof app.path) {
            app.path[0] = '\0';
        }
        wrote = snprintf(app.copy_path, sizeof app.copy_path, "%s/opened.md", dir);
        if (wrote < 0 || (size_t)wrote >= sizeof app.copy_path) {
            app.copy_path[0] = '\0';
        }
        wrote = snprintf(app.last_path, sizeof app.last_path, "%s/last", dir);
        if (wrote < 0 || (size_t)wrote >= sizeof app.last_path) {
            app.last_path[0] = '\0';
        }
        wrote = snprintf(app.export_path, sizeof app.export_path, "%s/export.html", dir);
        if (wrote < 0 || (size_t)wrote >= sizeof app.export_path) {
            app.export_path[0] = '\0';
        }
        wrote = snprintf(app.prefs_path, sizeof app.prefs_path, "%s/prefs", dir);
        if (wrote < 0 || (size_t)wrote >= sizeof app.prefs_path) {
            app.prefs_path[0] = '\0';
        }
        wrote = snprintf(app.font_path, sizeof app.font_path, "%s/custom.ttf", dir);
        if (wrote < 0 || (size_t)wrote >= sizeof app.font_path) {
            app.font_path[0] = '\0';
        }
    }
    load_prefs(&app);
    if (!page_readonly()) {
        page_set_editing(1);
    }
    refresh_session(&app);
    static const char *k_faces[] = {"LiberationSans-Regular.ttf", "LiberationSerif-Regular.ttf",
                                    "LiberationMono-Regular.ttf"};
    for (int i = 0; i < 3; i++) {
        app.fonts[i] = load_asset(android->activity->assetManager, k_faces[i], &app.font_lens[i]);
    }
    if (app.face < 0 || app.face > 3) {
        app.face = 0;
    }
    if (app.face == 3 && apply_custom_font(&app) == 0) {
        /* The chosen file is the face. */
    } else {
        if (app.face < 0 || app.face > 2) {
            app.face = 0;
        }
        if (app.fonts[app.face] == NULL || page_set_font(app.fonts[app.face], app.font_lens[app.face]) != 0) {
            app.face = 0;
            if (app.fonts[0] == NULL || page_set_font(app.fonts[0], app.font_lens[0]) != 0) {
                LOGE("font failed");
            }
        }
    }
    android->userData = &app;
    android->onAppCmd = on_cmd;
    android->onInputEvent = on_input;
    if (pipe(g_ime_fd) == 0) {
        fcntl(g_ime_fd[0], F_SETFL, O_NONBLOCK);
        fcntl(g_ime_fd[1], F_SETFL, O_NONBLOCK);
        ALooper_addFd(android->looper, g_ime_fd[0], LOOPER_ID_IME, ALOOPER_EVENT_INPUT, NULL, NULL);
    } else {
        g_ime_fd[0] = -1;
        g_ime_fd[1] = -1;
    }

    while (!android->destroyRequested) {
        int events = 0;
        struct android_poll_source *source = NULL;
        int wait = -1;
        if (app.glide && !app.drag) {
            wait = 0;
        } else if (app.edge != 0 && app.drag_mode == 2) {
            wait = 32;
        } else if (app.drag && app.drag_mode == 0) {
            int64_t left = app.drag_at + HOLD_MS - now_ms();
            wait = left < 1 ? 1 : (left > 1000 ? 1000 : (int)left);
        } else if (app.tap_pending) {
            int64_t left = app.tap_at + TAP_MS - now_ms();
            wait = left < 1 ? 1 : (left > 1000 ? 1000 : (int)left);
        }
        int ident = ALooper_pollOnce(wait, NULL, &events, (void **)&source);
        if (ident == ALOOPER_POLL_TIMEOUT && app.tap_pending && now_ms() - app.tap_at >= TAP_MS) {
            app.tap_pending = 0;
            if (!page_readonly() && doc_caret(app.doc) == app.tap_off && doc_toggle_task(app.doc, app.tap_off) == 0) {
                save_doc(&app);
                page_set_follow(0);
                render(&app);
            }
        } else if (ident == ALOOPER_POLL_TIMEOUT && app.drag && app.drag_mode == 0 && !app.settings &&
                   app.bar_hit < CHROME_BOLD && now_ms() - app.drag_at >= HOLD_MS) {
            arm_select(&app);
        } else if (ident == ALOOPER_POLL_TIMEOUT && app.glide && !app.drag) {
            int64_t now = now_ms();
            int elapsed = (int)(now - app.glide_at);
            if (elapsed < 0) {
                elapsed = 0;
            }
            float step = page_fling_step(elapsed) + app.glide_frac;
            int px = (int)step;
            app.glide_frac = step - (float)px;
            int rows = page_center_drag(px);
            if (rows != 0) {
                int step_row = rows > 0 ? 1 : -1;
                int n = rows > 0 ? rows : -rows;
                for (int i = 0; i < n && i < 8; i++) {
                    doc_move_row(app.doc, step_row);
                }
                app.compose_n = 0;
            }
            if (!page_fling_active()) {
                app.glide = 0;
                app.glide_carry = 0.f;
            }
            render(&app);
        } else if (ident == ALOOPER_POLL_TIMEOUT && app.edge != 0 && app.drag_mode == 2) {
            page_scroll_by(app.edge > 0 ? 64 : -64);
            size_t len = 0;
            const char *text = doc_text(app.doc, &len);
            doc_seek(app.doc, page_hit(app.chrome.text_x, app.chrome.text_y, app.chrome.text_w, app.chrome.text_h, text,
                                       len, doc_caret(app.doc), app.font_px, app.edge_x, app.edge_y));
            app.compose_n = 0;
            render(&app);
        } else if (ident == LOOPER_ID_IME) {
            char buf[32];
            while (read(g_ime_fd[0], buf, sizeof buf) > 0) {
            }
            apply_ime(&app);
            int reload = g_reload;
            g_reload = 0;
            int redraw = g_redraw;
            g_redraw = 0;
            if (g_font_pick) {
                g_font_pick = 0;
                if (apply_custom_font(&app) == 0) {
                    save_prefs(&app);
                    render(&app);
                } else {
                    LOGE("font file did not load");
                }
            }
            if (reload == 2 && write_uri(&app) == 0) {
                refresh_session(&app);
            } else if (reload == 1) {
                refresh_session(&app);
            }
            int back = g_back;
            g_back = 0;
            if (reload != 0) {
                app.settings = 0;
                app.contents = 0;
                render(&app);
            } else if (back) {
                handle_back(&app);
            } else if (redraw) {
                render(&app);
            }
        } else if (ident >= 0 && source != NULL) {
            source->process(android, source);
        }
    }
    if (g_ime_fd[0] >= 0) {
        close(g_ime_fd[0]);
        close(g_ime_fd[1]);
        g_ime_fd[0] = -1;
        g_ime_fd[1] = -1;
    }
    save_doc(&app);
    stop_gl(&app);
    doc_free(app.doc);
    for (int i = 0; i < 3; i++) {
        free(app.fonts[i]);
    }
    free(app.custom);
}
