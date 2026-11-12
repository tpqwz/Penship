#ifndef PENSHIP_FONTLIST_H
#define PENSHIP_FONTLIST_H

#include <stddef.h>

/* A face name. The file comes from Google Fonts as TrueType, or from the Monaspace repo. */
enum { FONT_NAME_MAX = 64, FONT_LIST_MAX = 48 };

/* 1 when name is a single family of letters, digits, spaces, and hyphens. */
int fontlist_name_ok(const char *name);

/* A file name turned into a face name. Strips a path and a .ttf, .otf, or .ttc suffix.
   Returns 1 and writes out, or 0 when the result is empty or a generic family. */
int fontlist_face_name(const char *raw, char *out, size_t cap);

/* Copy up to cap family names from CSS @font-face rules.
   A repeated family is kept once. Returns the count, or -1 when css or names is NULL. */
int fontlist_collect(const char *css, size_t len, char names[][FONT_NAME_MAX], int cap);

#endif
