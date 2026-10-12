#include "internal.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>

quantapdf_status quantapdf_load_page(
    quantapdf_document *document,
    int page_index,
    quantapdf_page **out_page)
{
    quantapdf_page *page;
    quantapdf_status status;

    if (out_page == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_page = NULL;

    if (document == NULL || page_index < 0)
        return QUANTAPDF_ERROR_ARGUMENT;

    page = (quantapdf_page *)calloc(1, sizeof(*page));
    if (page == NULL)
        return QUANTAPDF_ERROR_NOMEM;

    page->document = document;
    page->page_index = page_index;
    status = document->ops->load_page(
        document->backend, page_index, &page->backend);
    if (status != QUANTAPDF_OK) {
        free(page);
        return status;
    }
    *out_page = page;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_page_bounds(
    quantapdf_page *page,
    quantapdf_rect *out_bounds)
{
    quantapdf_rect bounds;
    quantapdf_status status;
    double user_unit;
    double width;
    double height;

    if (page == NULL || out_bounds == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    status = page->document->ops->page_bounds(page->backend, &bounds);
    if (status != QUANTAPDF_OK)
        return status;
    status = quantapdf_document_page_user_unit(
        page->document, page->page_index, &user_unit);
    if (status != QUANTAPDF_OK)
        return status;
    width = (double)bounds.x1 * user_unit;
    height = (double)bounds.y1 * user_unit;
    if (!isfinite(width) || !isfinite(height) ||
        width <= 0.0 || height <= 0.0 ||
        width > FLT_MAX || height > FLT_MAX)
        return QUANTAPDF_ERROR_FORMAT;
    out_bounds->x0 = 0.0f;
    out_bounds->y0 = 0.0f;
    out_bounds->x1 = (float)width;
    out_bounds->y1 = (float)height;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_page_box_bounds(
    quantapdf_page *page,
    quantapdf_page_box box,
    quantapdf_rect *out_bounds)
{
    quantapdf_status status;
    double user_unit;

    if (page == NULL || out_bounds == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    status = quantapdf_document_page_user_unit(
        page->document, page->page_index, &user_unit);
    if (status != QUANTAPDF_OK)
        return status;
    status = page->document->ops->page_box_bounds(
        page->document->backend,
        page->page_index,
        box,
        out_bounds);
    if (status != QUANTAPDF_OK)
        return status;
    out_bounds->x0 = (float)((double)out_bounds->x0 * user_unit);
    out_bounds->y0 = (float)((double)out_bounds->y0 * user_unit);
    out_bounds->x1 = (float)((double)out_bounds->x1 * user_unit);
    out_bounds->y1 = (float)((double)out_bounds->y1 * user_unit);
    return QUANTAPDF_OK;
}

void quantapdf_drop_page(quantapdf_page *page)
{
    if (page == NULL)
        return;

    page->document->ops->drop_page(page->backend);
    free(page);
}
