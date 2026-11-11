#ifndef PENSHIP_FONTLIST_H
#define PENSHIP_FONTLIST_H

#include <stddef.h>

/* A face name from a site stylesheet. The painter asks Google Fonts for TrueType;
   the sites themselves ship WOFF2, which this build cannot read. */
enum { FONT_NAME_MAX = 64, FONT_LIST_MAX = 48 };

/* 1 when name is a single family of letters, digits, spaces, and hyphens. */
int fontlist_name_ok(const char *name);

/* Copy up to cap family names from CSS @font-face rules. A comment
   `penship-host: name` prefixes the following families as "host | family".
   Returns the count, or -1 when css or names is NULL. */
int fontlist_collect(const char *css, size_t len, char names[][FONT_NAME_MAX], int cap);

#endif
