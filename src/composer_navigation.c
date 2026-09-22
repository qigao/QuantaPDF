#include "internal.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int quantapdf_navigation_rect_valid(const quantapdf_rect *rect)
{
    return rect != NULL &&
        isfinite(rect->x0) && isfinite(rect->y0) &&
        isfinite(rect->x1) && isfinite(rect->y1) &&
        rect->x1 > rect->x0 && rect->y1 > rect->y0;
}

static int quantapdf_navigation_point_valid(quantapdf_point point)
{
    return isfinite(point.x) && isfinite(point.y);
}

static int quantapdf_navigation_utf8_valid(const char *text)
{
    const unsigned char *cursor = (const unsigned char *)text;

    if (text == NULL || *text == '\0')
        return 0;
    while (*cursor != 0u) {
        uint32_t codepoint;
        size_t count;
        size_t i;

        if (*cursor < 0x80u) {
            ++cursor;
            continue;
        }
        if (*cursor >= 0xc2u && *cursor <= 0xdfu) {
            codepoint = (uint32_t)(*cursor & 0x1fu);
            count = 2u;
        } else if (*cursor >= 0xe0u && *cursor <= 0xefu) {
            codepoint = (uint32_t)(*cursor & 0x0fu);
            count = 3u;
        } else if (*cursor >= 0xf0u && *cursor <= 0xf4u) {
            codepoint = (uint32_t)(*cursor & 0x07u);
            count = 4u;
        } else {
            return 0;
        }
        for (i = 1u; i < count; ++i) {
            if (cursor[i] == 0u || (cursor[i] & 0xc0u) != 0x80u)
                return 0;
            codepoint =
                (codepoint << 6u) | (uint32_t)(cursor[i] & 0x3fu);
        }
        if ((count == 2u && codepoint < 0x80u) ||
            (count == 3u && codepoint < 0x800u) ||
            (count == 4u && codepoint < 0x10000u) ||
            (codepoint >= 0xd800u && codepoint <= 0xdfffu) ||
            codepoint > 0x10ffffu)
            return 0;
        cursor += count;
    }
    return 1;
}

static quantapdf_status quantapdf_navigation_check_capacity(
    const quantapdf_composer *composer)
{
    if (composer->link_count > SIZE_MAX - composer->outline_count)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if (composer->link_count + composer->outline_count >=
        composer->max_navigation_items)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_navigation_reserve_link(
    quantapdf_composer *composer)
{
    quantapdf_composer_link_state *grown;
    size_t capacity;
    quantapdf_status status;

    status = quantapdf_navigation_check_capacity(composer);
    if (status != QUANTAPDF_OK)
        return status;
    if (composer->link_count < composer->link_capacity)
        return QUANTAPDF_OK;

    capacity = composer->link_capacity == 0u
        ? 8u
        : composer->link_capacity * 2u;
    if (capacity < composer->link_capacity ||
        capacity > composer->max_navigation_items)
        capacity = composer->max_navigation_items;
    if (capacity == 0u || capacity > SIZE_MAX / sizeof(*grown))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    grown = (quantapdf_composer_link_state *)realloc(
        composer->links, capacity * sizeof(*grown));
    if (grown == NULL)
        return QUANTAPDF_ERROR_NOMEM;
    composer->links = grown;
    composer->link_capacity = capacity;
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_navigation_reserve_outline(
    quantapdf_composer *composer)
{
    quantapdf_composer_outline_state *grown;
    size_t capacity;
    quantapdf_status status;

    status = quantapdf_navigation_check_capacity(composer);
    if (status != QUANTAPDF_OK)
        return status;
    if (composer->outline_count >= (size_t)UINT32_MAX)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if (composer->outline_count < composer->outline_capacity)
        return QUANTAPDF_OK;

    capacity = composer->outline_capacity == 0u
        ? 8u
        : composer->outline_capacity * 2u;
    if (capacity < composer->outline_capacity ||
        capacity > composer->max_navigation_items ||
        capacity > (size_t)UINT32_MAX)
        capacity = composer->max_navigation_items < (size_t)UINT32_MAX
            ? composer->max_navigation_items
            : (size_t)UINT32_MAX;
    if (capacity == 0u || capacity > SIZE_MAX / sizeof(*grown))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    grown = (quantapdf_composer_outline_state *)realloc(
        composer->outlines, capacity * sizeof(*grown));
    if (grown == NULL)
        return QUANTAPDF_ERROR_NOMEM;
    composer->outlines = grown;
    composer->outline_capacity = capacity;
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_navigation_copy_string(
    quantapdf_composer *composer,
    const char *value,
    char **out_copy,
    size_t *out_size)
{
    size_t size;
    char *copy;

    *out_copy = NULL;
    *out_size = 0u;
    if (!quantapdf_navigation_utf8_valid(value))
        return QUANTAPDF_ERROR_FORMAT;
    size = strlen(value) + 1u;
    if (composer->resource_bytes > composer->max_resource_bytes ||
        size > composer->max_resource_bytes - composer->resource_bytes)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    copy = (char *)malloc(size);
    if (copy == NULL)
        return QUANTAPDF_ERROR_NOMEM;
    memcpy(copy, value, size);
    *out_copy = copy;
    *out_size = size;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_composer_add_uri_link(
    quantapdf_composer *composer,
    size_t page_index,
    const quantapdf_rect *hotspot,
    const char *uri_utf8)
{
    quantapdf_composer_link_state link;
    quantapdf_status status;
    size_t string_size = 0u;

    if (composer == NULL || page_index >= composer->page_count ||
        !quantapdf_navigation_rect_valid(hotspot) || uri_utf8 == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    memset(&link, 0, sizeof(link));
    status = quantapdf_navigation_copy_string(
        composer, uri_utf8, &link.uri_utf8, &string_size);
    if (status != QUANTAPDF_OK)
        return status;
    status = quantapdf_navigation_reserve_link(composer);
    if (status != QUANTAPDF_OK) {
        free(link.uri_utf8);
        return status;
    }

    link.page_index = page_index;
    link.hotspot = *hotspot;
    link.kind = QUANTAPDF_COMPOSER_LINK_URI_INTERNAL;
    composer->links[composer->link_count++] = link;
    composer->resource_bytes += string_size;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_composer_add_page_link(
    quantapdf_composer *composer,
    size_t page_index,
    const quantapdf_rect *hotspot,
    size_t target_page_index,
    quantapdf_point target)
{
    quantapdf_composer_link_state link;
    quantapdf_status status;

    if (composer == NULL || page_index >= composer->page_count ||
        target_page_index >= composer->page_count ||
        !quantapdf_navigation_rect_valid(hotspot) ||
        !quantapdf_navigation_point_valid(target))
        return QUANTAPDF_ERROR_ARGUMENT;

    status = quantapdf_navigation_reserve_link(composer);
    if (status != QUANTAPDF_OK)
        return status;
    memset(&link, 0, sizeof(link));
    link.page_index = page_index;
    link.hotspot = *hotspot;
    link.kind = QUANTAPDF_COMPOSER_LINK_PAGE_INTERNAL;
    link.target_page_index = target_page_index;
    link.target = target;
    composer->links[composer->link_count++] = link;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_composer_add_outline(
    quantapdf_composer *composer,
    const char *title_utf8,
    const quantapdf_composer_outline_options *options,
    quantapdf_composer_outline_id *out_outline_id)
{
    quantapdf_composer_outline_state outline;
    quantapdf_status status;
    size_t string_size = 0u;

    if (out_outline_id != NULL)
        *out_outline_id = 0u;
    if (composer == NULL || title_utf8 == NULL || options == NULL ||
        out_outline_id == NULL ||
        options->struct_size <
            QUANTAPDF_COMPOSER_OUTLINE_OPTIONS_V1_MIN_SIZE ||
        options->target_page_index >= composer->page_count ||
        !quantapdf_navigation_point_valid(options->target) ||
        (options->is_open != 0 && options->is_open != 1) ||
        (options->parent_id != 0u &&
         (size_t)options->parent_id > composer->outline_count))
        return QUANTAPDF_ERROR_ARGUMENT;

    memset(&outline, 0, sizeof(outline));
    status = quantapdf_navigation_copy_string(
        composer, title_utf8, &outline.title_utf8, &string_size);
    if (status != QUANTAPDF_OK)
        return status;
    status = quantapdf_navigation_reserve_outline(composer);
    if (status != QUANTAPDF_OK) {
        free(outline.title_utf8);
        return status;
    }

    outline.parent_id = options->parent_id;
    outline.target_page_index = options->target_page_index;
    outline.target = options->target;
    outline.is_open = options->is_open;
    composer->outlines[composer->outline_count] = outline;
    ++composer->outline_count;
    composer->resource_bytes += string_size;
    *out_outline_id =
        (quantapdf_composer_outline_id)composer->outline_count;
    return QUANTAPDF_OK;
}
