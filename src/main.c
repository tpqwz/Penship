#include "chrome.h"
#include "doc.h"
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
    char open_id[2048];
    int session;
    unsigned char *fonts[3];
    int font_lens[3];
    int face;
    int step;
    int settings;
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
    chrome_place(&app->chrome, width, height, inset_top, inset_bottom, density, app->settings);
    size_t len = 0;
    const char *text = doc_text(app->doc, &len);
    /* Count before a settings frame, which paints an empty page and would report zero words. */
    page_draw(app->pixels, width, height, app->chrome.text_x, app->chrome.text_y, app->chrome.text_w,
              app->chrome.text_h, text, len, doc_caret(app->doc), doc_anchor(app->doc), app->font_px);
    int words = page_words();
    if (app->settings) {
        page_draw(app->pixels, width, height, 0, 0, 1, 1, "", 0, 0, 0, app->font_px);
    }
    const char *labels[CHROME_COUNT];
    for (int i = 0; i < CHROME_COUNT; i++) {
        labels[i] = i == CHROME_SETTINGS ? "" : chrome_label(i);
    }
    page_draw_labels(app->pixels, width, height, CHROME_COUNT, app->chrome.x, app->chrome.y, app->chrome.w,
                     app->chrome.h, labels, app->chrome.vertical, text_px(density, 0));
    if (app->settings && app->chrome.h[CHROME_OPEN] > 0) {
        char stat[48];
        snprintf(stat, sizeof stat, words == 1 ? "1 word" : "%d words", words);
        const char *stat_label = stat;
        int stat_px = text_px(density, 0);
        int stat_w = stat_px * 8;
        int row_w = app->chrome.w[CHROME_OPEN];
        if (stat_w > row_w / 2) {
            stat_w = row_w / 2;
        }
        int stat_x = app->chrome.x[CHROME_OPEN] + row_w - stat_w;
        int stat_y = app->chrome.y[CHROME_OPEN];
        int stat_h = app->chrome.h[CHROME_OPEN];
        page_draw_labels(app->pixels, width, height, 1, &stat_x, &stat_y, &stat_w, &stat_h, &stat_label, 0, stat_px);
    }
    page_draw_menu(app->pixels, width, height, app->chrome.x[CHROME_SETTINGS], app->chrome.y[CHROME_SETTINGS],
                   app->chrome.w[CHROME_SETTINGS]);
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

static volatile int g_reload;

static void save_doc(struct App *app) {
    if (app->doc == NULL) {
        return;
    }
    if (app->session == SESSION_URI) {
        if (write_uri(app) == 0) {
            return;
        }
        /* shortcut: a read-only open saves files/opened.md and is not merged back later. */
        LOGE("save to opened file failed");
        if (app->copy_path[0] != '\0' && doc_save(app->doc, app->copy_path) != 0) {
            LOGE("save copy failed");
        }
        return;
    }
    if (app->session == SESSION_NOTE && app->path[0] != '\0' && doc_save(app->doc, app->path) != 0) {
        LOGE("save failed");
    }
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
    if (app->path[0] == '\0' || doc_load(app->doc, app->path) != 0) {
        doc_load_bytes(app->doc, "", 0);
    } else {
        size_t len = 0;
        doc_text(app->doc, &len);
        LOGI("loaded %zu bytes", len);
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
            if (doc_undo(app->doc) == 0) {
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
            if (copy_selection(app) && doc_backspace(app->doc) == 0) {
                changed = 1;
            }
        } else if (op.kind == IME_PASTE) {
            app->compose_n = 0;
            if (paste_clip(app)) {
                changed = 1;
            }
        } else if (op.kind == IME_TAB) {
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
        } else if (op.kind == IME_ERASE || op.kind == IME_FORWARD) {
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
        } else if (op.kind == IME_TEXT) {
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

/* A held press selects from the finger. The anchor stays while the drag grows. */
static void arm_select(struct App *app) {
    size_t len = 0;
    const char *text = doc_text(app->doc, &len);
    size_t at = page_hit(app->chrome.text_x, app->chrome.text_y, app->chrome.text_w, app->chrome.text_h, text, len,
                         doc_caret(app->doc), app->font_px, app->drag_x, app->drag_y);
    doc_place(app->doc, at);
    app->compose_n = 0;
    app->drag_mode = 2;
    page_set_follow(0);
    render(app);
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
        if (action == AMOTION_EVENT_ACTION_DOWN) {
            if (chrome_hit(&app->chrome, x, y) >= 0) {
                app->drag = 0;
                return 0;
            }
            app->drag = 1;
            app->drag_x = x;
            app->drag_y = y;
            app->drag_last = y;
            app->drag_mode = 0;
            app->drag_at = now_ms();
            app->edge = 0;
            return 1;
        }
        if (action == AMOTION_EVENT_ACTION_CANCEL) {
            app->drag = 0;
            app->edge = 0;
            return 1;
        }
        if (action == AMOTION_EVENT_ACTION_MOVE && app->drag) {
            int dx = x - app->drag_x;
            int dy = y - app->drag_y;
            if (dx < 0) {
                dx = -dx;
            }
            if (dy < 0) {
                dy = -dy;
            }
            /* A quick drag scrolls, unless it starts on the caret. A held press selects. No fling. */
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
                int rows = page_center_drag(app->drag_last - y);
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
            int selecting = app->drag_mode == 2;
            app->drag = 0;
            app->edge = 0;
            if (dragged) {
                app->tap_pending = 0;
                app->tap_at = 0;
                if (selecting) {
                    show_keyboard(app);
                }
                return 1;
            }
            int hit = chrome_hit(&app->chrome, x, y);
            if (hit >= 0) {
                app->tap_pending = 0;
                app->tap_at = 0;
            }
            if (hit == CHROME_SETTINGS) {
                app->settings = 1;
            } else if (hit == CHROME_OPEN) {
                app->settings = 0;
                pick_file(app, 0);
            } else if (hit == CHROME_SAVE) {
                save_doc(app);
                app->settings = 0;
                if (app->session != SESSION_URI) {
                    pick_file(app, 1);
                }
            } else if (hit == CHROME_DONE) {
                app->settings = 0;
            } else if (app->settings && hit < 0) {
                render(app);
                return 1;
            } else if (hit == CHROME_SMALLER && app->step > 0) {
                app->step--;
            } else if (hit == CHROME_LARGER && app->step + 1 < text_step_count()) {
                app->step++;
            } else if (hit == CHROME_FONT) {
                for (int n = 0; n < 3; n++) {
                    app->face = (app->face + 1) % 3;
                    if (app->fonts[app->face] != NULL && page_set_font(app->fonts[app->face], app->font_lens[app->face]) == 0) {
                        break;
                    }
                }
            } else if (hit == CHROME_TYPEWRITER) {
                page_set_typewriter(!page_typewriter());
            } else if (hit == CHROME_FOCUS) {
                page_set_focus(!page_focus());
            } else if (hit == CHROME_THEME) {
                page_set_theme(page_theme() == 0 ? 1 : 0);
            } else if (hit == CHROME_SOURCE) {
                page_set_source(!page_source());
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
                    doc_place(app->doc, at);
                    page_set_editing(1);
                } else {
                    if (!(page_editing() && page_same_unit(text, len, doc_caret(app->doc), at))) {
                        page_set_editing(0);
                    }
                    doc_place(app->doc, at);
                    app->tap_pending = 1;
                    app->tap_off = doc_caret(app->doc);
                }
                app->compose_n = 0;
                /* A tap stays on this page. Dragging is what scrolls. */
                page_set_follow(0);
                render(app);
                show_keyboard(app);
                return 1;
            }
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
        if (app->settings) {
            app->settings = 0;
            render(app);
            return 1;
        }
        return 0;
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
        if (copy_selection(app) && doc_backspace(app->doc) == 0) {
            save_doc(app);
            page_set_follow(1);
            render(app);
        }
        return 1;
    }
    if (ctrl && !shift && code == AKEYCODE_V) {
        app->compose_n = 0;
        if (paste_clip(app)) {
            save_doc(app);
            page_set_follow(1);
            render(app);
        }
        return 1;
    }
    if (ctrl && code == AKEYCODE_Z) {
        if (doc_undo(app->doc) == 0) {
            save_doc(app);
            page_set_follow(1);
            render(app);
        }
        return 1;
    }
    if (code == AKEYCODE_TAB) {
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
    if (code == AKEYCODE_DEL || code == AKEYCODE_FORWARD_DEL) {
        app->compose_n = 0;
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
        show_keyboard(app);
        render(app);
        break;
    case APP_CMD_PAUSE:
        save_doc(app);
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

void android_main(struct android_app *android) {
    struct App app;
    memset(&app, 0, sizeof app);
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
    }
    refresh_session(&app);
    static const char *k_faces[] = {"LiberationSans-Regular.ttf", "LiberationSerif-Regular.ttf",
                                    "LiberationMono-Regular.ttf"};
    for (int i = 0; i < 3; i++) {
        app.fonts[i] = load_asset(android->activity->assetManager, k_faces[i], &app.font_lens[i]);
    }
    if (app.fonts[0] == NULL || page_set_font(app.fonts[0], app.font_lens[0]) != 0) {
        LOGE("font failed");
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
        if (app.edge != 0 && app.drag_mode == 2) {
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
            if (doc_caret(app.doc) == app.tap_off && doc_toggle_task(app.doc, app.tap_off) == 0) {
                save_doc(&app);
                page_set_follow(0);
                render(&app);
            }
        } else if (ident == ALOOPER_POLL_TIMEOUT && app.drag && app.drag_mode == 0 && now_ms() - app.drag_at >= HOLD_MS) {
            arm_select(&app);
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
            if (reload == 2 && write_uri(&app) == 0) {
                refresh_session(&app);
            } else if (reload == 1) {
                refresh_session(&app);
            }
            if (reload != 0) {
                app.settings = 0;
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
}
