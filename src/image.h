#ifndef PENSHIP_IMAGE_H
#define PENSHIP_IMAGE_H

#include <stddef.h>

/* Decode a PNG or a JPEG into tightly packed RGBA. *rgba is malloc'd. 0 on success.
   PNG may be gray, gray with alpha, palette, or color, including 16-bit samples and Adam7 interlacing.
   JPEG may be baseline or progressive. Arithmetic JPEG is refused. */
int image_decode(const unsigned char *bytes, int n, unsigned char **rgba, int *w, int *h);

#endif
