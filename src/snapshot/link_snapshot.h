#ifndef QUANTAPDF_LINK_SNAPSHOT_H
#define QUANTAPDF_LINK_SNAPSHOT_H

#include <stddef.h>

#include "snapshot_owner.h"

typedef struct quantapdf_link_internal {
    quantapdf_rect hotspot;
    quantapdf_link_kind kind;
    int target_page;
    quantapdf_point target;
    char *uri;
    size_t uri_size;
} quantapdf_link_internal;

struct quantapdf_link_page {
    quantapdf_snapshot snapshot;
    quantapdf_link_internal *items;
    size_t count;
};

#endif
