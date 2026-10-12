#include "validation.h"
#include "ir/ir.h"

#include <math.h>
#include <string.h>

static void reset_report(quantapdf_validation_report *report)
{
    if (report != NULL)
        memset(report, 0, sizeof(*report));
}

static quantapdf_status fail(
    quantapdf_validation_report *report, quantapdf_validation_rule rule,
    quantapdf_validation_subject subject, size_t index, size_t detail)
{
    report->rule = rule;
    report->subject = subject;
    report->index = index;
    report->detail = detail;
    return QUANTAPDF_ERROR_BACKEND;
}

static int array_valid(const void *data, size_t count, size_t element_size)
{
    return count <= SIZE_MAX / element_size && (count == 0 || data != NULL);
}

static int point_valid(quantapdf_point point)
{
    return isfinite(point.x) && isfinite(point.y);
}

static int transform_valid(quantapdf_affine_transform transform)
{
    return isfinite(transform.a) && isfinite(transform.b) &&
        isfinite(transform.c) && isfinite(transform.d) &&
        isfinite(transform.e) && isfinite(transform.f);
}

static int rect_valid(quantapdf_rect rect)
{
    return isfinite(rect.x0) && isfinite(rect.y0) &&
        isfinite(rect.x1) && isfinite(rect.y1) &&
        rect.x1 > rect.x0 && rect.y1 > rect.y0;
}

int quantapdf_validation_path_valid(
    const quantapdf_composer_path_command *commands, size_t count)
{
    size_t i;
    int have_current = 0, have_segment = 0;
    if (count == 0 || !array_valid(commands, count, sizeof(*commands)))
        return 0;
    for (i = 0; i < count; ++i) {
        const quantapdf_composer_path_command *command = &commands[i];
        switch (command->kind) {
        case QUANTAPDF_COMPOSER_PATH_MOVE_TO:
            if (!point_valid(command->point1))
                return 0;
            have_current = 1;
            break;
        case QUANTAPDF_COMPOSER_PATH_LINE_TO:
            if (!have_current || !point_valid(command->point1))
                return 0;
            have_segment = 1;
            break;
        case QUANTAPDF_COMPOSER_PATH_CUBIC_TO:
            if (!have_current || !point_valid(command->point1) ||
                !point_valid(command->point2) || !point_valid(command->point3))
                return 0;
            have_segment = 1;
            break;
        case QUANTAPDF_COMPOSER_PATH_CLOSE:
            if (!have_current)
                return 0;
            break;
        default:
            return 0;
        }
    }
    return have_segment;
}

static int required_id(size_t id, size_t count)
{
    return id != 0 && id <= count;
}

/* Nested Clip intersections are a bounded borrowed DAG. Validation does
 * not allocate or mutate model state. The stack catches active back-edges
 * while permitting shared leaves/subgraphs. A total-visit ceiling prevents
 * pathological DAG expansion during validation and export. */
static int clip_dag_acyclic(
    const quantapdf_resource_registry *registry, size_t id,
    size_t *stack, size_t depth, size_t *visits)
{
    const quantapdf_composer_clip_state *clip;
    size_t i;
    if (id == 0u || id > registry->clip_count ||
        depth >= 8u || *visits >= 1024u)
        return 0;
    ++*visits;
    for (i = 0u; i < depth; ++i) {
        if (stack[i] == id)
            return 0;
    }
    clip = &registry->clips[id - 1u];
    if (clip->kind == QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL)
        return 1;
    if (clip->kind != QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL ||
        clip->members == NULL || clip->member_count == 0u)
        return 0;
    stack[depth] = id;
    for (i = 0u; i < clip->member_count; ++i) {
        if (!clip_dag_acyclic(
                registry, (size_t)clip->members[i], stack, depth + 1u, visits))
            return 0;
    }
    return 1;
}

/* Macros only attach record coordinates to a failed predicate. They do not
 * evaluate the predicate twice or hide allocation, traversal, or mutation. */
#define REQUIRE(test, rule, subject, index, detail) do { \
    if (!(test)) return fail(report, rule, subject, index, detail); \
} while (0)
#define STORAGE(data, count, subject) \
    REQUIRE(array_valid(data, count, sizeof(*(data))), \
        QUANTAPDF_VALIDATION_STORAGE, subject, 0, 0)
#define RESOURCE_STORAGE(field, count, capacity, subject) do { \
    STORAGE(r->field, r->count, subject); \
    REQUIRE(r->count <= r->capacity, QUANTAPDF_VALIDATION_STORAGE, subject, 0, 0); \
} while (0)
#define VALUE(test, subject, index) \
    REQUIRE(test, QUANTAPDF_VALIDATION_VALUE, subject, index, 0)
#define REF(test, subject, index, id) \
    REQUIRE(test, QUANTAPDF_VALIDATION_REFERENCE, subject, index, id)

quantapdf_status quantapdf_validate_ir(
    const quantapdf_ir *ir, quantapdf_validation_report *report)
{
    const quantapdf_resource_registry *r;
    size_t i, j;
    reset_report(report);
    if (ir == NULL || report == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    r = &ir->resources;
    STORAGE(ir->pages, ir->page_count, QUANTAPDF_VALIDATION_PAGE);
    STORAGE(ir->operations, ir->operation_count, QUANTAPDF_VALIDATION_OPERATION);
    STORAGE(ir->links, ir->link_count, QUANTAPDF_VALIDATION_LINK);
    STORAGE(ir->outlines, ir->outline_count, QUANTAPDF_VALIDATION_OUTLINE);
    RESOURCE_STORAGE(images, image_count, image_capacity, QUANTAPDF_VALIDATION_IMAGE);
    RESOURCE_STORAGE(fonts, font_count, font_capacity, QUANTAPDF_VALIDATION_FONT);
    RESOURCE_STORAGE(graphics_states, graphics_state_count, graphics_state_capacity,
        QUANTAPDF_VALIDATION_GRAPHICS_STATE);
    RESOURCE_STORAGE(soft_masks, soft_mask_count, soft_mask_capacity, QUANTAPDF_VALIDATION_SOFT_MASK);
    RESOURCE_STORAGE(clips, clip_count, clip_capacity, QUANTAPDF_VALIDATION_CLIP);
    RESOURCE_STORAGE(paints, paint_count, paint_capacity, QUANTAPDF_VALIDATION_PAINT);
    RESOURCE_STORAGE(forms, form_count, form_capacity, QUANTAPDF_VALIDATION_FORM);
    VALUE(r->image_count <= UINT32_MAX, QUANTAPDF_VALIDATION_IMAGE, 0);
    VALUE(r->font_count <= UINT32_MAX, QUANTAPDF_VALIDATION_FONT, 0);
    VALUE(ir->outline_count <= UINT32_MAX, QUANTAPDF_VALIDATION_OUTLINE, 0);

    for (i = 0; i < ir->page_count; ++i) {
        const quantapdf_composer_page_state *p = &ir->pages[i];
        VALUE(isfinite(p->width_points) && p->width_points > 0 &&
            isfinite(p->height_points) && p->height_points > 0,
            QUANTAPDF_VALIDATION_PAGE, i);
    }
    for (i = 0; i < r->image_count; ++i) {
        const quantapdf_composer_image_state *v = &r->images[i];
        VALUE(v->data != NULL && v->size != 0 && v->width != 0 && v->height != 0 &&
            (v->components == 1 || v->components == 3) &&
            v->format >= QUANTAPDF_COMPOSER_IMAGE_FORMAT_JPEG &&
            v->format <= QUANTAPDF_COMPOSER_IMAGE_FORMAT_RAW &&
            (v->has_alpha == 0 || v->has_alpha == 1) &&
            v->alpha_size <= SIZE_MAX - v->size &&
            (v->has_alpha ? v->alpha_data != NULL && v->alpha_size != 0 : v->alpha_size == 0),
            QUANTAPDF_VALIDATION_IMAGE, i);
    }
    for (i = 0; i < r->font_count; ++i)
        VALUE(r->fonts[i].data != NULL && r->fonts[i].size != 0, QUANTAPDF_VALIDATION_FONT, i);
    for (i = 0; i < r->graphics_state_count; ++i) {
        const quantapdf_composer_graphics_state *v = &r->graphics_states[i];
        VALUE(isfinite(v->fill_alpha) && v->fill_alpha >= 0 && v->fill_alpha <= 1 &&
            isfinite(v->stroke_alpha) && v->stroke_alpha >= 0 && v->stroke_alpha <= 1 &&
            v->blend_mode >= QUANTAPDF_COMPOSER_BLEND_NORMAL &&
            v->blend_mode <= QUANTAPDF_COMPOSER_BLEND_LIGHTEN,
            QUANTAPDF_VALIDATION_GRAPHICS_STATE, i);
        REF(v->clip_id <= r->clip_count, QUANTAPDF_VALIDATION_GRAPHICS_STATE, i, v->clip_id);
        REF(v->soft_mask_id <= r->soft_mask_count, QUANTAPDF_VALIDATION_GRAPHICS_STATE, i, v->soft_mask_id);
    }
    for (i = 0; i < r->soft_mask_count; ++i) {
        const quantapdf_composer_soft_mask_state *v = &r->soft_masks[i];
        REF(required_id(v->form_id, r->form_count), QUANTAPDF_VALIDATION_SOFT_MASK, i, v->form_id);
        VALUE((r->forms[v->form_id - 1].flags & QUANTAPDF_COMPOSER_FORM_FLAG_TRANSPARENCY_GROUP) != 0 &&
            transform_valid(v->transform) && v->mode >= QUANTAPDF_COMPOSER_SOFT_MASK_ALPHA &&
            v->mode <= QUANTAPDF_COMPOSER_SOFT_MASK_LUMINOSITY, QUANTAPDF_VALIDATION_SOFT_MASK, i);
    }
    for (i = 0; i < r->clip_count; ++i) {
        const quantapdf_composer_clip_state *v = &r->clips[i];
        if (v->kind == QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL) {
            VALUE(quantapdf_validation_path_valid(v->commands, v->command_count) &&
                transform_valid(v->transform) && v->fill_rule >= QUANTAPDF_COMPOSER_FILL_NONZERO &&
                v->fill_rule <= QUANTAPDF_COMPOSER_FILL_EVEN_ODD, QUANTAPDF_VALIDATION_CLIP, i);
        } else {
            VALUE(v->kind == QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL &&
                v->member_count != 0 && array_valid(v->members, v->member_count, sizeof(*v->members)),
                QUANTAPDF_VALIDATION_CLIP, i);
            for (j = 0; j < v->member_count; ++j) {
                size_t id = v->members[j];
                REF(required_id(id, r->clip_count), QUANTAPDF_VALIDATION_CLIP, i, id);
                /* Members may be Clip Paths OR nested intersections; the
                 * traversal gate below rejects cycles/unsupported graphs. */
            }
        }
    }
    /* Validate every nested graph once as a possible root. Flat legacy
     * intersection resources continue to pass this same bounded gate. */
    for (i = 0; i < r->clip_count; ++i) {
        if (r->clips[i].kind == QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL) {
            size_t stack[8] = {0};
            size_t visits = 0u;
            VALUE(clip_dag_acyclic(r, i + 1u, stack, 0u, &visits),
                  QUANTAPDF_VALIDATION_CLIP, i);
        }
    }
    for (i = 0; i < r->paint_count; ++i) {
        const quantapdf_composer_paint_state *v = &r->paints[i];
        VALUE(transform_valid(v->transform), QUANTAPDF_VALIDATION_PAINT, i);
        if (v->kind == QUANTAPDF_COMPOSER_PAINT_TILING_PATTERN_INTERNAL) {
            VALUE(v->pdf_data != NULL && v->pdf_size != 0 &&
                isfinite(v->tile_width) && v->tile_width > 0 &&
                isfinite(v->tile_height) && v->tile_height > 0 &&
                isfinite(v->x_step) && v->x_step != 0 && isfinite(v->y_step) && v->y_step != 0,
                QUANTAPDF_VALIDATION_PAINT, i);
        } else {
            VALUE((v->kind == QUANTAPDF_COMPOSER_PAINT_LINEAR_GRADIENT_INTERNAL ||
                v->kind == QUANTAPDF_COMPOSER_PAINT_RADIAL_GRADIENT_INTERNAL) &&
                point_valid(v->start) && point_valid(v->end) &&
                isfinite(v->start_radius) && v->start_radius >= 0 &&
                isfinite(v->end_radius) && v->end_radius >= 0 &&
                v->stop_count >= 2 && array_valid(v->stops, v->stop_count, sizeof(*v->stops)),
                QUANTAPDF_VALIDATION_PAINT, i);
            for (j = 0; j < v->stop_count; ++j)
                VALUE(isfinite(v->stops[j].offset) && v->stops[j].offset >= 0 &&
                    v->stops[j].offset <= 1 && (j == 0 || v->stops[j].offset >= v->stops[j - 1].offset),
                    QUANTAPDF_VALIDATION_PAINT, i);
        }
    }
    for (i = 0; i < r->form_count; ++i) {
        const quantapdf_composer_form_state *v = &r->forms[i];
        VALUE(v->pdf_data != NULL && v->pdf_size != 0 &&
            isfinite(v->width_points) && v->width_points > 0 &&
            isfinite(v->height_points) && v->height_points > 0, QUANTAPDF_VALIDATION_FORM, i);
    }
    for (i = 0; i < ir->operation_count; ++i) {
        const quantapdf_composer_operation *v = &ir->operations[i];
        REF(v->page_index < ir->page_count, QUANTAPDF_VALIDATION_OPERATION, i, v->page_index);
        REF(v->graphics_state_id <= r->graphics_state_count, QUANTAPDF_VALIDATION_OPERATION, i, v->graphics_state_id);
        switch (v->kind) {
        case QUANTAPDF_COMPOSER_OPERATION_TEXT:
            VALUE(v->value.text.text_utf8 != NULL && rect_valid(v->bounds) &&
                transform_valid(v->value.text.transform), QUANTAPDF_VALIDATION_OPERATION, i);
            break;
        case QUANTAPDF_COMPOSER_OPERATION_IMAGE:
            REF(required_id(v->value.image.image_id, r->image_count), QUANTAPDF_VALIDATION_OPERATION, i, v->value.image.image_id);
            VALUE(rect_valid(v->bounds), QUANTAPDF_VALIDATION_OPERATION, i);
            break;
        case QUANTAPDF_COMPOSER_OPERATION_PATH: {
            const quantapdf_composer_path_operation *p = &v->value.path;
            VALUE(quantapdf_validation_path_valid(p->commands, p->command_count) &&
                array_valid(p->dash_lengths, p->dash_count, sizeof(*p->dash_lengths)) &&
                isfinite(p->dash_phase), QUANTAPDF_VALIDATION_OPERATION, i);
            REF(p->options.fill_paint_id <= r->paint_count, QUANTAPDF_VALIDATION_OPERATION, i, p->options.fill_paint_id);
            REF(p->options.stroke_paint_id <= r->paint_count, QUANTAPDF_VALIDATION_OPERATION, i, p->options.stroke_paint_id);
            for (j = 0; j < p->dash_count; ++j)
                VALUE(isfinite(p->dash_lengths[j]) && p->dash_lengths[j] >= 0, QUANTAPDF_VALIDATION_OPERATION, i);
            break;
        }
        case QUANTAPDF_COMPOSER_OPERATION_EMBEDDED_TEXT:
            REF(required_id(v->value.embedded_text.options.font_id, r->font_count),
                QUANTAPDF_VALIDATION_OPERATION, i, v->value.embedded_text.options.font_id);
            VALUE(v->value.embedded_text.text_utf8 != NULL && rect_valid(v->bounds) &&
                transform_valid(v->value.embedded_text.transform), QUANTAPDF_VALIDATION_OPERATION, i);
            break;
        case QUANTAPDF_COMPOSER_OPERATION_GLYPH_RUN:
            REF(required_id(v->value.glyph_run.options.font_id, r->font_count),
                QUANTAPDF_VALIDATION_OPERATION, i, v->value.glyph_run.options.font_id);
            VALUE(v->value.glyph_run.glyph_count != 0 &&
                array_valid(v->value.glyph_run.glyphs, v->value.glyph_run.glyph_count, sizeof(*v->value.glyph_run.glyphs)) &&
                array_valid(v->value.glyph_run.unicode_utf8, v->value.glyph_run.unicode_size, 1) &&
                point_valid(v->value.glyph_run.origin) && transform_valid(v->value.glyph_run.transform),
                QUANTAPDF_VALIDATION_OPERATION, i);
            break;
        case QUANTAPDF_COMPOSER_OPERATION_FORM:
            REF(required_id(v->value.form.form_id, r->form_count), QUANTAPDF_VALIDATION_OPERATION, i, v->value.form.form_id);
            VALUE(transform_valid(v->value.form.transform), QUANTAPDF_VALIDATION_OPERATION, i);
            break;
        default:
            return fail(report, QUANTAPDF_VALIDATION_VALUE, QUANTAPDF_VALIDATION_OPERATION, i, 0);
        }
    }
    for (i = 0; i < ir->link_count; ++i) {
        const quantapdf_composer_link_state *v = &ir->links[i];
        REF(v->page_index < ir->page_count, QUANTAPDF_VALIDATION_LINK, i, v->page_index);
        VALUE(rect_valid(v->hotspot), QUANTAPDF_VALIDATION_LINK, i);
        if (v->kind == QUANTAPDF_COMPOSER_LINK_PAGE_INTERNAL) {
            REF(v->target_page_index < ir->page_count, QUANTAPDF_VALIDATION_LINK, i, v->target_page_index);
            VALUE(point_valid(v->target), QUANTAPDF_VALIDATION_LINK, i);
        } else {
            VALUE(v->kind == QUANTAPDF_COMPOSER_LINK_URI_INTERNAL && v->uri_utf8 != NULL,
                QUANTAPDF_VALIDATION_LINK, i);
        }
    }
    for (i = 0; i < ir->outline_count; ++i) {
        const quantapdf_composer_outline_state *v = &ir->outlines[i];
        /* Parents precede children in the append-only IR, precluding cycles. */
        REQUIRE(v->parent_id <= i, QUANTAPDF_VALIDATION_ANCESTRY, QUANTAPDF_VALIDATION_OUTLINE, i, v->parent_id);
        REF(v->target_page_index < ir->page_count, QUANTAPDF_VALIDATION_OUTLINE, i, v->target_page_index);
        VALUE(v->title_utf8 != NULL && point_valid(v->target), QUANTAPDF_VALIDATION_OUTLINE, i);
    }
    return QUANTAPDF_OK;
}

#undef REF
#undef VALUE
#undef RESOURCE_STORAGE
#undef STORAGE
#undef REQUIRE

static int name_is(const char *name, size_t size, const char *literal, size_t literal_size)
{
    return name != NULL && size == literal_size && memcmp(name, literal, size) == 0;
}
#define NAME_IS(literal) name_is(name, size, literal, sizeof(literal) - 1)

uint32_t quantapdf_validation_action_finding(const char *name, size_t size)
{
    if (NAME_IS("/JavaScript"))
        return QUANTAPDF_AUDIT_JAVASCRIPT_ACTION;
    if (NAME_IS("/Launch"))
        return QUANTAPDF_AUDIT_LAUNCH_ACTION;
    if (NAME_IS("/URI") || NAME_IS("/GoToR") || NAME_IS("/GoToE") ||
        NAME_IS("/SubmitForm") || NAME_IS("/ImportData"))
        return QUANTAPDF_AUDIT_EXTERNAL_ACTION;
    return NAME_IS("/GoTo") ? 0 : QUANTAPDF_AUDIT_OTHER_ACTION;
}

int quantapdf_validation_rich_media(const char *name, size_t size)
{
    return NAME_IS("/RichMedia") || NAME_IS("/3D") || NAME_IS("/Movie") ||
        NAME_IS("/Sound") || NAME_IS("/Screen");
}
#undef NAME_IS

quantapdf_status quantapdf_validate_security(
    uint32_t observed, uint32_t forbidden, quantapdf_validation_report *report)
{
    const uint32_t known = QUANTAPDF_AUDIT_JAVASCRIPT_ACTION |
        QUANTAPDF_AUDIT_LAUNCH_ACTION | QUANTAPDF_AUDIT_EXTERNAL_ACTION |
        QUANTAPDF_AUDIT_OTHER_ACTION | QUANTAPDF_AUDIT_EMBEDDED_FILE |
        QUANTAPDF_AUDIT_XFA | QUANTAPDF_AUDIT_RICH_MEDIA |
        QUANTAPDF_AUDIT_SIGNATURE | QUANTAPDF_AUDIT_ENCRYPTION;
    reset_report(report);
    if (report == NULL || (forbidden & ~known) != 0)
        return QUANTAPDF_ERROR_ARGUMENT;
    if ((observed & ~known) != 0)
        return fail(report, QUANTAPDF_VALIDATION_UNKNOWN_FINDING,
            QUANTAPDF_VALIDATION_SECURITY, 0, observed & ~known);
    if ((observed & forbidden) != 0) {
        (void)fail(report, QUANTAPDF_VALIDATION_SECURITY_POLICY,
            QUANTAPDF_VALIDATION_SECURITY, 0, observed & forbidden);
        return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    return QUANTAPDF_OK;
}
