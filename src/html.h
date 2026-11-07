#ifndef PENSHIP_HTML_H
#define PENSHIP_HTML_H

#include <stddef.h>

/* Write an HTML page for the markdown in text. *out is malloc'd. Returns 0, or -1. */
int html_export(const char *text, size_t n, char **out, size_t *out_len);

#endif
