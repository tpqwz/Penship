#ifndef PENSHIP_IMAGE_H
#define PENSHIP_IMAGE_H

#include <stddef.h>

/* Decode an 8-bit PNG or a baseline JPEG into tightly packed RGBA. *rgba is malloc'd. 0 on success. */
int image_decode(const unsigned char *bytes, int n, unsigned char **rgba, int *w, int *h);

#endif
