#ifndef QUANTAPDF_OUTLINE_SNAPSHOT_H
#define QUANTAPDF_OUTLINE_SNAPSHOT_H

#include <stddef.h>

#include "snapshot_owner.h"

typedef struct quantapdf_outline_node_internal {
    size_t parent_index;
    size_t first_child_index;
    size_t next_sibling_index;
    quantapdf_outline_destination_kind destination_kind;
    int target_page;
    quantapdf_point target;
    size_t title_offset;
    size_t title_size;
    size_t uri_offset;
    size_t uri_size;
    int has_title;
    int is_open;
} quantapdf_outline_node_internal;

struct quantapdf_outline {
    quantapdf_snapshot snapshot;
    quantapdf_outline_node_internal *nodes;
    char *strings;
    size_t count;
    size_t string_size;
};

#endif
