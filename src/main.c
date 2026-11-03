#include "page.h"

#include <android/log.h>
#include <android_native_app_glue.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include <stdlib.h>
#include <string.h>

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

struct App {
    struct android_app *android;
    EGLDisplay display;
    EGLSurface surface;
    EGLContext context;
    GLuint program;
    GLuint texture;
    int32_t width;
    int32_t height;
    uint8_t *pixels;
    int ready;
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
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glClearColor(PAGE_PAPER_R / 255.f, PAGE_PAPER_G / 255.f, PAGE_PAPER_B / 255.f, 1.f);
    app->ready = 1;
    return 1;
}

static void draw(struct App *app) {
    if (!app->ready) {
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
        page_draw(app->pixels, width, height);
        glBindTexture(GL_TEXTURE_2D, app->texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, app->pixels);
        LOGI("page %d x %d", width, height);
    }
    glViewport(0, 0, width, height);
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

static void on_cmd(struct android_app *android, int32_t cmd) {
    struct App *app = android->userData;
    switch (cmd) {
    case APP_CMD_INIT_WINDOW:
        if (start_gl(app)) {
            draw(app);
        }
        break;
    case APP_CMD_TERM_WINDOW:
        stop_gl(app);
        break;
    case APP_CMD_WINDOW_RESIZED:
    case APP_CMD_WINDOW_REDRAW_NEEDED:
    case APP_CMD_CONTENT_RECT_CHANGED:
    case APP_CMD_GAINED_FOCUS:
    case APP_CMD_RESUME:
        draw(app);
        break;
    default:
        break;
    }
}

void android_main(struct android_app *android) {
    struct App app;
    memset(&app, 0, sizeof app);
    app.android = android;
    app.display = EGL_NO_DISPLAY;
    app.surface = EGL_NO_SURFACE;
    app.context = EGL_NO_CONTEXT;
    android->userData = &app;
    android->onAppCmd = on_cmd;

    while (!android->destroyRequested) {
        int events = 0;
        struct android_poll_source *source = NULL;
        int ident = ALooper_pollOnce(-1, NULL, &events, (void **)&source);
        if (ident >= 0 && source != NULL) {
            source->process(android, source);
        }
    }
    stop_gl(&app);
}
