#ifndef QUANTAPDF_PAGE_BACKEND_INTERNAL_H
#define QUANTAPDF_PAGE_BACKEND_INTERNAL_H

#include "../backend/backend_internal.h"

/*
 * Backend-neutral page lifecycle boundary.
 *
 * Page implementations should keep provider-specific state private and
 * access it through backend dispatch.
 */

quantapdf_status
quantapdf_backend_page_open_internal(
    quantapdf_backend *backend,
    int page_index,
    void **out_page);

void
quantapdf_backend_page_close_internal(
    quantapdf_backend *backend,
    void *page);

#endif
