#include "validation/validation.h"
#include "ir/ir.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(test) do { if (!(test)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test); \
    exit(EXIT_FAILURE); \
} } while (0)

/* All storage is real and addressable. Opaque encoded bytes are intentionally
 * not parsed: provider admission is outside the pure model validator. */
typedef struct model_fixture {
    quantapdf_ir ir;
    quantapdf_composer_page_state page;
    quantapdf_composer_image_state image;
    quantapdf_composer_font_state font;
    quantapdf_composer_graphics_state state;
    quantapdf_composer_soft_mask_state mask;
    quantapdf_composer_clip_state clips[2];
    quantapdf_composer_paint_state paints[3];
    quantapdf_composer_form_state form;
    quantapdf_composer_operation operations[6];
    quantapdf_composer_link_state links[2];
    quantapdf_composer_outline_state outlines[2];
    quantapdf_composer_path_command commands[2];
    quantapdf_composer_clip_id member;
    quantapdf_composer_gradient_stop stops[2];
    quantapdf_composer_glyph glyph;
    unsigned char bytes[8];
} model_fixture;

static void initialize(model_fixture *f)
{
    quantapdf_affine_transform identity = {1, 0, 0, 1, 0, 0};
    quantapdf_rect bounds = {0, 0, 10, 10};
    quantapdf_resource_registry *r;
    size_t i;
    memset(f, 0, sizeof(*f));
    f->page.width_points = f->page.height_points = 100;
    f->ir.pages = &f->page;
    f->ir.page_count = 1;
    f->ir.operations = f->operations;
    f->ir.operation_count = 6;
    f->ir.links = f->links;
    f->ir.link_count = 2;
    f->ir.outlines = f->outlines;
    f->ir.outline_count = 2;
    r = &f->ir.resources;
    r->images = &f->image; r->image_count = r->image_capacity = 1;
    r->fonts = &f->font; r->font_count = r->font_capacity = 1;
    r->graphics_states = &f->state; r->graphics_state_count = r->graphics_state_capacity = 1;
    r->soft_masks = &f->mask; r->soft_mask_count = r->soft_mask_capacity = 1;
    r->clips = f->clips; r->clip_count = r->clip_capacity = 2;
    r->paints = f->paints; r->paint_count = r->paint_capacity = 3;
    r->forms = &f->form; r->form_count = r->form_capacity = 1;
    f->image.data = f->bytes; f->image.size = 3;
    f->image.width = f->image.height = 1; f->image.components = 3;
    f->image.format = QUANTAPDF_COMPOSER_IMAGE_FORMAT_RAW;
    f->font.data = f->bytes; f->font.size = sizeof(f->bytes);
    f->state.fill_alpha = f->state.stroke_alpha = 1;
    f->state.clip_id = 2; f->state.soft_mask_id = 1;
    f->mask.form_id = 1; f->mask.transform = identity;
    f->form.pdf_data = f->bytes; f->form.pdf_size = sizeof(f->bytes);
    f->form.width_points = f->form.height_points = 10;
    f->form.flags = QUANTAPDF_COMPOSER_FORM_FLAG_TRANSPARENCY_GROUP;
    f->commands[0].kind = QUANTAPDF_COMPOSER_PATH_MOVE_TO;
    f->commands[1].kind = QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f->commands[1].point1.x = 1;
    f->clips[0].kind = QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
    f->clips[0].commands = f->commands; f->clips[0].command_count = 2;
    f->clips[0].transform = identity;
    f->member = 1;
    f->clips[1].kind = QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL;
    f->clips[1].members = &f->member; f->clips[1].member_count = 1;
    f->stops[1].offset = 1;
    for (i = 0; i < 2; ++i) {
        f->paints[i].kind = i == 0 ? QUANTAPDF_COMPOSER_PAINT_LINEAR_GRADIENT_INTERNAL :
            QUANTAPDF_COMPOSER_PAINT_RADIAL_GRADIENT_INTERNAL;
        f->paints[i].transform = identity;
        f->paints[i].stops = f->stops; f->paints[i].stop_count = 2;
        f->paints[i].end.x = 10; f->paints[i].end_radius = 10;
    }
    f->paints[2].kind = QUANTAPDF_COMPOSER_PAINT_TILING_PATTERN_INTERNAL;
    f->paints[2].transform = identity;
    f->paints[2].pdf_data = f->bytes; f->paints[2].pdf_size = sizeof(f->bytes);
    f->paints[2].tile_width = f->paints[2].tile_height = 10;
    /* Negative steps are supported by the existing public pattern contract. */
    f->paints[2].x_step = -10; f->paints[2].y_step = 10;
    for (i = 0; i < 6; ++i) {
        f->operations[i].kind = (quantapdf_composer_operation_kind)(i + 1);
        f->operations[i].bounds = bounds;
        f->operations[i].graphics_state_id = 1;
    }
    f->operations[0].value.text.text_utf8 = "text";
    f->operations[0].value.text.transform = identity;
    f->operations[1].value.image.image_id = 1;
    f->operations[2].value.path.commands = f->commands;
    f->operations[2].value.path.command_count = 2;
    f->operations[2].value.path.options.fill_paint_id = 1;
    f->operations[2].value.path.options.stroke_paint_id = 3;
    f->operations[3].value.embedded_text.text_utf8 = "text";
    f->operations[3].value.embedded_text.options.font_id = 1;
    f->operations[3].value.embedded_text.transform = identity;
    f->operations[4].value.glyph_run.glyphs = &f->glyph;
    f->operations[4].value.glyph_run.glyph_count = 1;
    f->operations[4].value.glyph_run.options.font_id = 1;
    f->operations[4].value.glyph_run.transform = identity;
    f->operations[5].value.form.form_id = 1;
    f->operations[5].value.form.transform = identity;
    f->links[0].kind = QUANTAPDF_COMPOSER_LINK_URI_INTERNAL;
    f->links[0].uri_utf8 = "https://example.invalid/";
    f->links[0].hotspot = bounds;
    f->links[1].kind = QUANTAPDF_COMPOSER_LINK_PAGE_INTERNAL;
    f->links[1].hotspot = bounds;
    f->outlines[0].title_utf8 = "first";
    f->outlines[1].title_utf8 = "child"; f->outlines[1].parent_id = 1;
}

static void expect_failure(const quantapdf_ir *ir, quantapdf_validation_rule rule,
    quantapdf_validation_subject subject, size_t index, size_t detail)
{
    quantapdf_validation_report a, b;
    memset(&a, 0xa5, sizeof(a));
    memset(&b, 0x5a, sizeof(b));
    CHECK(quantapdf_validate_ir(ir, &a) == QUANTAPDF_ERROR_BACKEND);
    CHECK(quantapdf_validate_ir(ir, &b) == QUANTAPDF_ERROR_BACKEND);
    CHECK(a.rule == rule && a.subject == subject && a.index == index && a.detail == detail);
    CHECK(a.rule == b.rule && a.subject == b.subject && a.index == b.index && a.detail == b.detail);
}

static void test_models(void)
{
    model_fixture f;
    quantapdf_validation_report report;
    quantapdf_ir empty = {0};
    initialize(&f);
    CHECK(quantapdf_validate_ir(&f.ir, &report) == QUANTAPDF_OK);
    CHECK(report.rule == QUANTAPDF_VALIDATION_OK && report.subject == QUANTAPDF_VALIDATION_MODEL);
    CHECK(quantapdf_validate_ir(&empty, &report) == QUANTAPDF_OK);
    CHECK(quantapdf_validate_ir(NULL, &report) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(report.rule == QUANTAPDF_VALIDATION_OK && report.index == 0 && report.detail == 0);
    CHECK(quantapdf_validate_ir(&f.ir, NULL) == QUANTAPDF_ERROR_ARGUMENT);

#define BAD(field, value, rule, subject, index, detail) do { \
    initialize(&f); f.field = value; \
    expect_failure(&f.ir, rule, subject, index, detail); \
} while (0)
    BAD(ir.pages, NULL, QUANTAPDF_VALIDATION_STORAGE, QUANTAPDF_VALIDATION_PAGE, 0, 0);
    BAD(ir.operation_count, SIZE_MAX, QUANTAPDF_VALIDATION_STORAGE, QUANTAPDF_VALIDATION_OPERATION, 0, 0);
    BAD(ir.resources.image_capacity, 0, QUANTAPDF_VALIDATION_STORAGE, QUANTAPDF_VALIDATION_IMAGE, 0, 0);
    BAD(page.width_points, NAN, QUANTAPDF_VALIDATION_VALUE, QUANTAPDF_VALIDATION_PAGE, 0, 0);
    BAD(image.data, NULL, QUANTAPDF_VALIDATION_VALUE, QUANTAPDF_VALIDATION_IMAGE, 0, 0);
    BAD(font.size, 0, QUANTAPDF_VALIDATION_VALUE, QUANTAPDF_VALIDATION_FONT, 0, 0);
    BAD(state.clip_id, 3, QUANTAPDF_VALIDATION_REFERENCE, QUANTAPDF_VALIDATION_GRAPHICS_STATE, 0, 3);
    BAD(state.soft_mask_id, 2, QUANTAPDF_VALIDATION_REFERENCE, QUANTAPDF_VALIDATION_GRAPHICS_STATE, 0, 2);
    BAD(mask.form_id, 0, QUANTAPDF_VALIDATION_REFERENCE, QUANTAPDF_VALIDATION_SOFT_MASK, 0, 0);
    BAD(form.flags, 0, QUANTAPDF_VALIDATION_VALUE, QUANTAPDF_VALIDATION_SOFT_MASK, 0, 0);
    BAD(member, 2, QUANTAPDF_VALIDATION_REFERENCE, QUANTAPDF_VALIDATION_CLIP, 1, 2);
    BAD(member, 3, QUANTAPDF_VALIDATION_REFERENCE, QUANTAPDF_VALIDATION_CLIP, 1, 3);
    BAD(clips[0].command_count, SIZE_MAX, QUANTAPDF_VALIDATION_VALUE, QUANTAPDF_VALIDATION_CLIP, 0, 0);
    BAD(stops[1].offset, -1, QUANTAPDF_VALIDATION_VALUE, QUANTAPDF_VALIDATION_PAINT, 0, 0);
    BAD(paints[2].pdf_data, NULL, QUANTAPDF_VALIDATION_VALUE, QUANTAPDF_VALIDATION_PAINT, 2, 0);
    BAD(form.width_points, 0, QUANTAPDF_VALIDATION_VALUE, QUANTAPDF_VALIDATION_FORM, 0, 0);
    BAD(operations[0].page_index, 1, QUANTAPDF_VALIDATION_REFERENCE, QUANTAPDF_VALIDATION_OPERATION, 0, 1);
    BAD(operations[0].graphics_state_id, 2, QUANTAPDF_VALIDATION_REFERENCE, QUANTAPDF_VALIDATION_OPERATION, 0, 2);
    BAD(operations[1].value.image.image_id, 2, QUANTAPDF_VALIDATION_REFERENCE, QUANTAPDF_VALIDATION_OPERATION, 1, 2);
    BAD(operations[2].value.path.options.fill_paint_id, 4, QUANTAPDF_VALIDATION_REFERENCE, QUANTAPDF_VALIDATION_OPERATION, 2, 4);
    BAD(operations[2].value.path.options.stroke_paint_id, 4, QUANTAPDF_VALIDATION_REFERENCE, QUANTAPDF_VALIDATION_OPERATION, 2, 4);
    BAD(operations[3].value.embedded_text.options.font_id, 0, QUANTAPDF_VALIDATION_REFERENCE, QUANTAPDF_VALIDATION_OPERATION, 3, 0);
    BAD(operations[4].value.glyph_run.options.font_id, 2, QUANTAPDF_VALIDATION_REFERENCE, QUANTAPDF_VALIDATION_OPERATION, 4, 2);
    BAD(operations[5].value.form.form_id, 2, QUANTAPDF_VALIDATION_REFERENCE, QUANTAPDF_VALIDATION_OPERATION, 5, 2);
    BAD(links[0].page_index, 1, QUANTAPDF_VALIDATION_REFERENCE, QUANTAPDF_VALIDATION_LINK, 0, 1);
    BAD(links[1].target_page_index, 1, QUANTAPDF_VALIDATION_REFERENCE, QUANTAPDF_VALIDATION_LINK, 1, 1);
    BAD(outlines[0].parent_id, 1, QUANTAPDF_VALIDATION_ANCESTRY, QUANTAPDF_VALIDATION_OUTLINE, 0, 1);
    BAD(outlines[1].target_page_index, 1, QUANTAPDF_VALIDATION_REFERENCE, QUANTAPDF_VALIDATION_OUTLINE, 1, 1);
#undef BAD
    initialize(&f);
    f.page.width_points = 0;
    f.state.clip_id = 3;
    expect_failure(&f.ir, QUANTAPDF_VALIDATION_VALUE, QUANTAPDF_VALIDATION_PAGE, 0, 0);
    f.page.width_points = 100;
    expect_failure(&f.ir, QUANTAPDF_VALIDATION_REFERENCE, QUANTAPDF_VALIDATION_GRAPHICS_STATE, 0, 3);
    f.state.clip_id = 2;
    CHECK(quantapdf_validate_ir(&f.ir, &report) == QUANTAPDF_OK);
    CHECK(report.rule == QUANTAPDF_VALIDATION_OK && report.index == 0 && report.detail == 0);
}

static void test_report_storage_independence(void)
{
    model_fixture a, b;
    quantapdf_validation_report report;
    initialize(&a); initialize(&b);
    CHECK(a.ir.operations != b.ir.operations && a.ir.outlines != b.ir.outlines);
    a.operations[3].graphics_state_id = b.operations[3].graphics_state_id = 99;
    a.outlines[1].parent_id = b.outlines[1].parent_id = 2;
    /* Equal logical defects in independent storage have equal coordinates. */
    expect_failure(&a.ir, QUANTAPDF_VALIDATION_REFERENCE, QUANTAPDF_VALIDATION_OPERATION, 3, 99);
    expect_failure(&b.ir, QUANTAPDF_VALIDATION_REFERENCE, QUANTAPDF_VALIDATION_OPERATION, 3, 99);
    a.operations[3].graphics_state_id = 1;
    expect_failure(&a.ir, QUANTAPDF_VALIDATION_ANCESTRY, QUANTAPDF_VALIDATION_OUTLINE, 1, 2);
    /* Repairing one model must not alter the other model's first failure. */
    expect_failure(&b.ir, QUANTAPDF_VALIDATION_REFERENCE, QUANTAPDF_VALIDATION_OPERATION, 3, 99);
    b.operations[3].graphics_state_id = 1;
    expect_failure(&b.ir, QUANTAPDF_VALIDATION_ANCESTRY, QUANTAPDF_VALIDATION_OUTLINE, 1, 2);
    a.outlines[1].parent_id = 1;
    memset(&report, 0xff, sizeof(report));
    CHECK(quantapdf_validate_ir(&a.ir, &report) == QUANTAPDF_OK);
    CHECK(report.rule == QUANTAPDF_VALIDATION_OK && report.subject == QUANTAPDF_VALIDATION_MODEL);
    CHECK(report.index == 0 && report.detail == 0);
}

static void test_security(void)
{
    static const struct { const char *name; uint32_t flag; } names[] = {
        {"/JavaScript", QUANTAPDF_AUDIT_JAVASCRIPT_ACTION},
        {"/Launch", QUANTAPDF_AUDIT_LAUNCH_ACTION},
        {"/URI", QUANTAPDF_AUDIT_EXTERNAL_ACTION},
        {"/GoToR", QUANTAPDF_AUDIT_EXTERNAL_ACTION},
        {"/GoToE", QUANTAPDF_AUDIT_EXTERNAL_ACTION},
        {"/SubmitForm", QUANTAPDF_AUDIT_EXTERNAL_ACTION},
        {"/ImportData", QUANTAPDF_AUDIT_EXTERNAL_ACTION},
        {"/GoTo", 0}, {"/Unknown", QUANTAPDF_AUDIT_OTHER_ACTION}
    };
    static const char *rich[] = {"/RichMedia", "/3D", "/Movie", "/Sound", "/Screen"};
    const char borrowed[] = {'/', 'U', 'R', 'I', 'X'};
    const uint32_t all = (uint32_t)QUANTAPDF_SANITIZE_ALL |
        QUANTAPDF_AUDIT_SIGNATURE | QUANTAPDF_AUDIT_ENCRYPTION;
    quantapdf_validation_report report;
    size_t i;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        CHECK(quantapdf_validation_action_finding(names[i].name, strlen(names[i].name)) == names[i].flag);
    CHECK(quantapdf_validation_action_finding(borrowed, 4) == QUANTAPDF_AUDIT_EXTERNAL_ACTION);
    CHECK(quantapdf_validation_action_finding(borrowed, 5) == QUANTAPDF_AUDIT_OTHER_ACTION);
    for (i = 0; i < sizeof(rich) / sizeof(rich[0]); ++i)
        CHECK(quantapdf_validation_rich_media(rich[i], strlen(rich[i])));
    CHECK(!quantapdf_validation_rich_media("/Link", 5));
    for (i = 0; i < 9; ++i) {
        uint32_t bit = UINT32_C(1) << i;
        CHECK(quantapdf_validate_security(all, bit, &report) == QUANTAPDF_ERROR_UNSUPPORTED);
        CHECK(report.rule == QUANTAPDF_VALIDATION_SECURITY_POLICY && report.detail == bit);
        CHECK(quantapdf_validate_security(all & ~bit, bit, &report) == QUANTAPDF_OK);
        CHECK(report.rule == QUANTAPDF_VALIDATION_OK && report.detail == 0);
    }
    CHECK(quantapdf_validate_security(all, 0, &report) == QUANTAPDF_OK);
    CHECK(quantapdf_validate_security(all, QUANTAPDF_SANITIZE_ALL, &report) == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(report.detail == QUANTAPDF_SANITIZE_ALL);
    CHECK(quantapdf_validate_security(UINT32_MAX, 0, &report) == QUANTAPDF_ERROR_BACKEND);
    CHECK(report.rule == QUANTAPDF_VALIDATION_UNKNOWN_FINDING && report.detail == (UINT32_MAX & ~all));
    CHECK(quantapdf_validate_security(0, UINT32_MAX, &report) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(report.rule == QUANTAPDF_VALIDATION_OK && report.detail == 0);
    CHECK(quantapdf_validate_security(0, 0, NULL) == QUANTAPDF_ERROR_ARGUMENT);
}

int main(void)
{
    test_models();
    test_report_storage_independence();
    test_security();
    return EXIT_SUCCESS;
}
