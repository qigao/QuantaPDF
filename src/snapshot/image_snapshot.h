#ifndef QUANTAPDF_IMAGE_SNAPSHOT_H
#define QUANTAPDF_IMAGE_SNAPSHOT_H

#include <stddef.h>

#include "snapshot_owner.h"

typedef struct quantapdf_image_occurrence_internal {
    quantapdf_quad quad;
    unsigned char *pixels;
    size_t pixel_size;
    int pixel_width;
    int pixel_height;
    int pixel_stride;
    int pixel_components;
    int components;
    int bits_per_component;
    int has_alpha;
} quantapdf_image_occurrence_internal;

struct quantapdf_image_page {
    quantapdf_snapshot snapshot;
    quantapdf_image_occurrence_internal *items;
    size_t count;
};

#endif
