#include "resource/resource_registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr)                                                           \
    do {                                                                      \
        if (!(expr)) {                                                        \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                   \
                    __FILE__, __LINE__, #expr);                               \
            return 1;                                                         \
        }                                                                     \
    } while (0)

static unsigned char *bytes(unsigned char value, size_t size)
{
    unsigned char *data = (unsigned char *)malloc(size);
    if (data != NULL)
        memset(data, value, size);
    return data;
}

/* Opaque font/form bytes isolate ownership from provider parsing. */
static int publish_family(
    quantapdf_resource_registry *registry, unsigned char variant,
    size_t expected_id, int duplicate)
{
    quantapdf_composer_image_state image = {0};
    quantapdf_composer_font_state font = {0};
    quantapdf_composer_form_state form = {0};
    quantapdf_composer_clip_state clip = {0};
    quantapdf_composer_paint_state paint = {0};
    quantapdf_composer_soft_mask_state mask = {0};
    quantapdf_composer_graphics_state state = {0};
    quantapdf_composer_image_id image_id = 0;
    quantapdf_composer_font_id font_id = 0;
    size_t id = 0, added = SIZE_MAX;
    size_t available = duplicate ? 0 : 1024;

    image.data = bytes(variant, 3);
    image.alpha_data = bytes(255, 1);
    CHECK(image.data != NULL && image.alpha_data != NULL);
    image.size = 3;
    image.alpha_size = 1;
    image.width = image.height = 1;
    image.components = 3;
    image.has_alpha = 1;
    image.format = QUANTAPDF_COMPOSER_IMAGE_FORMAT_RAW;
    CHECK(quantapdf_resource_intern_image(registry, &image, available,
          &image_id, &added) == QUANTAPDF_OK);
    CHECK(image_id == expected_id && added == (duplicate ? 0u : 4u));
    CHECK(image.data == NULL && image.alpha_data == NULL);

    font.data = bytes(variant, 3);
    CHECK(font.data != NULL);
    font.size = 3;
    CHECK(quantapdf_resource_intern_font(registry, &font, available,
          &font_id, &added) == QUANTAPDF_OK);
    CHECK(font_id == expected_id && added == (duplicate ? 0u : 3u));
    CHECK(font.data == NULL);

    form.pdf_data = bytes(variant, 5);
    CHECK(form.pdf_data != NULL);
    form.pdf_size = 5;
    form.width_points = form.height_points = 10;
    form.flags = QUANTAPDF_COMPOSER_FORM_FLAG_TRANSPARENCY_GROUP;
    CHECK(quantapdf_resource_intern_form(registry, &form, available,
          &id, &added) == QUANTAPDF_OK);
    CHECK(id == expected_id && added == (duplicate ? 0u : 5u));
    CHECK(form.pdf_data == NULL);

    clip.kind = QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
    clip.commands = (quantapdf_composer_path_command *)calloc(3, sizeof(*clip.commands));
    CHECK(clip.commands != NULL);
    clip.command_count = 3;
    clip.commands[0].kind = QUANTAPDF_COMPOSER_PATH_MOVE_TO;
    clip.commands[1].kind = QUANTAPDF_COMPOSER_PATH_LINE_TO;
    clip.commands[1].point1.x = (float)variant + 1;
    clip.commands[2].kind = QUANTAPDF_COMPOSER_PATH_CLOSE;
    /* Unused control points must not participate in semantic identity. */
    clip.commands[2].point3.x = duplicate ? 456 : 123;
    clip.transform.a = clip.transform.d = 1;
    CHECK(quantapdf_resource_intern_clip(registry, &clip, available,
          &id, &added) == QUANTAPDF_OK);
    CHECK(id == expected_id);
    CHECK(added == (duplicate ? 0 : 3 * sizeof(quantapdf_composer_path_command)));
    CHECK(clip.commands == NULL);

    paint.kind = QUANTAPDF_COMPOSER_PAINT_LINEAR_GRADIENT_INTERNAL;
    paint.stops = (quantapdf_composer_gradient_stop *)calloc(2, sizeof(*paint.stops));
    CHECK(paint.stops != NULL);
    paint.stop_count = 2;
    paint.stops[0].offset = duplicate ? -0.0f : 0.0f;
    paint.stops[0].argb = UINT32_C(0xff000000) + variant;
    paint.stops[1].offset = 1;
    paint.stops[1].argb = UINT32_C(0xffffffff);
    paint.end.x = 10;
    paint.transform.a = paint.transform.d = 1;
    CHECK(quantapdf_resource_intern_paint(registry, &paint, available,
          &id, &added) == QUANTAPDF_OK);
    CHECK(id == expected_id);
    CHECK(added == (duplicate ? 0 : 2 * sizeof(quantapdf_composer_gradient_stop)));
    CHECK(paint.stops == NULL);

    mask.form_id = expected_id;
    mask.mode = QUANTAPDF_COMPOSER_SOFT_MASK_ALPHA;
    mask.transform.a = mask.transform.d = 1;
    CHECK(quantapdf_resource_intern_soft_mask(registry, &mask, 0,
          &id, &added) == QUANTAPDF_OK);
    CHECK(id == expected_id && added == 0 && mask.form_id == 0);
    state.clip_id = expected_id;
    state.soft_mask_id = expected_id;
    state.fill_alpha = state.stroke_alpha = 1;
    state.blend_mode = QUANTAPDF_COMPOSER_BLEND_NORMAL;
    CHECK(quantapdf_resource_intern_graphics_state(registry, &state, 0,
          &id, &added) == QUANTAPDF_OK);
    CHECK(id == expected_id && added == 0 && state.clip_id == 0);
    return 0;
}

static int test_reuse_and_nested_rewind(void)
{
    quantapdf_resource_registry registry = {0};
    quantapdf_resource_mark empty = quantapdf_resource_checkpoint(&registry);
    quantapdf_resource_mark first, second;
    size_t capacity;
    CHECK(publish_family(&registry, 0, 1, 0) == 0);
    first = quantapdf_resource_checkpoint(&registry);
    CHECK(publish_family(&registry, 0, 1, 1) == 0);
    CHECK(publish_family(&registry, 1, 2, 0) == 0);
    second = quantapdf_resource_checkpoint(&registry);
    CHECK(publish_family(&registry, 2, 3, 0) == 0);
    quantapdf_resource_rewind(&registry, second);
    CHECK(publish_family(&registry, 1, 2, 1) == 0);
    quantapdf_resource_rewind(&registry, first);
    CHECK(registry.image_count == 1 && registry.font_count == 1);
    CHECK(registry.form_count == 1 && registry.clip_count == 1);
    CHECK(registry.paint_count == 1 && registry.soft_mask_count == 1);
    CHECK(registry.graphics_state_count == 1);
    CHECK(publish_family(&registry, 0, 1, 1) == 0);
    CHECK(publish_family(&registry, 1, 2, 0) == 0);
    capacity = registry.image_capacity;
    quantapdf_resource_rewind(&registry, empty);
    CHECK(registry.image_count == 0 && registry.image_capacity == capacity);
    CHECK(registry.font_count == 0 && registry.form_count == 0);
    CHECK(registry.clip_count == 0 && registry.paint_count == 0);
    CHECK(registry.soft_mask_count == 0 && registry.graphics_state_count == 0);
    CHECK(publish_family(&registry, 2, 1, 0) == 0);
    quantapdf_resource_dispose(&registry);
    CHECK(registry.images == NULL && registry.image_capacity == 0);
    CHECK(registry.fonts == NULL && registry.forms == NULL && registry.clips == NULL);
    CHECK(registry.paints == NULL && registry.soft_masks == NULL);
    CHECK(registry.graphics_states == NULL);
    quantapdf_resource_dispose(&registry);
    quantapdf_resource_dispose(NULL);
    return 0;
}

static int test_failed_admission_preserves_registry(void)
{
    quantapdf_resource_registry registry = {0};
    quantapdf_composer_image_state image = {0};
    quantapdf_composer_image_id id = UINT32_MAX;
    quantapdf_composer_clip_state clip = {0};
    quantapdf_composer_paint_state paint = {0};
    size_t resource_id = SIZE_MAX;
    size_t added = SIZE_MAX;
    CHECK(publish_family(&registry, 0, 1, 0) == 0);
    image.data = bytes(2, 3);
    CHECK(image.data != NULL);
    image.size = 3;
    image.width = image.height = 1;
    image.components = 3;
    image.format = QUANTAPDF_COMPOSER_IMAGE_FORMAT_RAW;
    CHECK(quantapdf_resource_intern_image(&registry, &image, 2, &id, &added) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(id == 0 && added == 0 && image.data == NULL);
    CHECK(registry.image_count == 1);

    image.data = bytes(2, 1);
    image.alpha_data = bytes(3, 1);
    CHECK(image.data != NULL && image.alpha_data != NULL);
    image.size = SIZE_MAX;
    image.alpha_size = 1;
    CHECK(quantapdf_resource_intern_image(&registry, &image, SIZE_MAX,
          &id, &added) == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(id == 0 && added == 0 && image.data == NULL && image.alpha_data == NULL);
    CHECK(registry.image_count == 1);
    CHECK(publish_family(&registry, 0, 1, 1) == 0);

    clip.commands = (quantapdf_composer_path_command *)malloc(sizeof(*clip.commands));
    CHECK(clip.commands != NULL);
    clip.command_count = SIZE_MAX / sizeof(*clip.commands) + 1;
    CHECK(quantapdf_resource_intern_clip(&registry, &clip, SIZE_MAX,
          &resource_id, &added) == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(resource_id == 0 && added == 0 && clip.commands == NULL);
    CHECK(registry.clip_count == 1);

    paint.stops = (quantapdf_composer_gradient_stop *)malloc(sizeof(*paint.stops));
    CHECK(paint.stops != NULL);
    paint.kind = QUANTAPDF_COMPOSER_PAINT_LINEAR_GRADIENT_INTERNAL;
    paint.stop_count = SIZE_MAX / sizeof(*paint.stops) + 1;
    CHECK(quantapdf_resource_intern_paint(&registry, &paint, SIZE_MAX,
          &resource_id, &added) == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(resource_id == 0 && added == 0 && paint.stops == NULL);
    CHECK(registry.paint_count == 1);
    CHECK(publish_family(&registry, 0, 1, 1) == 0);

    image.data = bytes(4, 1);
    CHECK(image.data != NULL);
    CHECK(quantapdf_resource_intern_image(NULL, &image, 0, &id, &added) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(image.data != NULL && id == 0 && added == 0);
    free(image.data);
    quantapdf_resource_dispose(&registry);
    return 0;
}

int main(void)
{
    CHECK(test_reuse_and_nested_rewind() == 0);
    CHECK(test_failed_admission_preserves_registry() == 0);
    return 0;
}
