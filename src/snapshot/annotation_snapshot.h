#ifndef QUANTAPDF_ANNOTATION_SNAPSHOT_H
#define QUANTAPDF_ANNOTATION_SNAPSHOT_H

#include <stddef.h>
#include <stdint.h>

#include "snapshot_owner.h"

typedef struct quantapdf_annotation_internal {
    quantapdf_annotation_type type;
    quantapdf_rect bounds;
    uint32_t flags;
    size_t contents_offset;
    size_t contents_size;
    int has_contents;
} quantapdf_annotation_internal;

struct quantapdf_annotation_page {
    quantapdf_snapshot snapshot;
    quantapdf_annotation_internal *items;
    char *strings;
    size_t count;
    size_t string_size;
};

#endif
