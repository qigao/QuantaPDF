#include "graph_page_geometry.h"

#include <math.h>
#include <string.h>

/* Extracted verbatim from graph_page_geometry.c: provider-neutral,
 * allocation-free page display mapping usable by isolated C11 tests. */
quantapdf_status quantapdf_graph_page_display_map(
    const quantapdf_graph_page_geometry *geometry,
    quantapdf_graph_page_display *out_display)
{
    double const *m, *c;
    double unit, crop_width, crop_height;
    quantapdf_graph_page_display value = {0};
    if (out_display != NULL) memset(out_display, 0, sizeof(*out_display));
    if (out_display == NULL || geometry == NULL) return QUANTAPDF_ERROR_ARGUMENT;
    m = geometry->media_box;
    c = geometry->crop_box;
    unit = geometry->user_unit;
    if (!isfinite(unit) || unit <= 0.0 ||
        (geometry->rotation != 0 && geometry->rotation != 90 &&
         geometry->rotation != 180 && geometry->rotation != 270))
        return QUANTAPDF_ERROR_ARGUMENT;
    for (size_t i = 0; i < 4; ++i)
        if (!isfinite(m[i]) || !isfinite(c[i]))
            return QUANTAPDF_ERROR_ARGUMENT;
    if (m[2] <= m[0] || m[3] <= m[1] ||
        c[2] <= c[0] || c[3] <= c[1])
        return QUANTAPDF_ERROR_ARGUMENT;

    /* Do not model a potentially provider-specific crop/media intersection. */
    if (c[0] < m[0] || c[1] < m[1] ||
        c[2] > m[2] || c[3] > m[3])
        return QUANTAPDF_ERROR_UNSUPPORTED;

    crop_width = c[2] - c[0];
    crop_height = c[3] - c[1];
    if (!isfinite(crop_width) || !isfinite(crop_height))
        return QUANTAPDF_ERROR_UNSUPPORTED;

    if (geometry->rotation == 90 || geometry->rotation == 270) {
        value.width_points = crop_height * unit;
        value.height_points = crop_width * unit;
    } else {
        value.width_points = crop_width * unit;
        value.height_points = crop_height * unit;
    }
    if (!isfinite(value.width_points) || !isfinite(value.height_points) ||
        value.width_points <= 0.0 || value.height_points <= 0.0)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    /* Source PDF Y-up coordinates -> zero-origin output PDF Y-up coordinates.
     * 90 means clockwise rotation when looking at the displayed page. */
    switch (geometry->rotation) {
    case 0:
        value.from_source[0] = unit;
        value.from_source[3] = unit;
        value.from_source[4] = -c[0] * unit;
        value.from_source[5] = -c[1] * unit;
        break;
    case 90:
        value.from_source[1] = -unit;
        value.from_source[2] = unit;
        value.from_source[4] = -c[1] * unit;
        value.from_source[5] = c[2] * unit;
        break;
    case 180:
        value.from_source[0] = -unit;
        value.from_source[3] = -unit;
        value.from_source[4] = c[2] * unit;
        value.from_source[5] = c[3] * unit;
        break;
    case 270:
        value.from_source[1] = unit;
        value.from_source[2] = -unit;
        value.from_source[4] = c[3] * unit;
        value.from_source[5] = -c[0] * unit;
        break;
    default:
        return QUANTAPDF_ERROR_ARGUMENT;
    }
    for (size_t i = 0; i < 6; ++i)
        if (!isfinite(value.from_source[i]))
            return QUANTAPDF_ERROR_UNSUPPORTED;
    *out_display = value;
    return QUANTAPDF_OK;
}
