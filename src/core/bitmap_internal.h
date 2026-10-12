#ifndef QUANTAPDF_BITMAP_INTERNAL_H
#define QUANTAPDF_BITMAP_INTERNAL_H

#include <quantapdf/quantapdf.h>

struct quantapdf_bitmap {
    quantapdf_context *context;
    unsigned char *data;
    size_t size;
    int width;
    int height;
    int stride;
    int components;
};

#endif
