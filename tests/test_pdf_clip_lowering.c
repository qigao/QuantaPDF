#include "ir/pdf_path_lowering.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(EXIT_FAILURE); } } while(0)
#define OK(x) CHECK((x) == QUANTAPDF_OK)

static quantapdf_pdf_content_limits budget(void)
{
    quantapdf_pdf_content_limits value = {4096, 512, 8};
    return value;
}
static quantapdf_pdf_path_lower_limits lower_budget(void)
{
    quantapdf_pdf_path_lower_limits value = {32, 256};
    return value;
}

static void parse(const char *source, quantapdf_pdf_page_semantics *page)
{
    quantapdf_pdf_content_limits limits = budget();
    memset(page, 0, sizeof(*page));
    OK(quantapdf_pdf_content_parse((const unsigned char *)source,
        strlen(source), &limits, &page->content));
    OK(quantapdf_pdf_graphics_evaluate(page->content,
        &limits, &page->graphics));
}
static void drop(quantapdf_pdf_page_semantics *page)
{
    quantapdf_pdf_graphics_dispose(page->graphics);
    quantapdf_pdf_content_dispose(page->content);
}

static void test_nested_clip_scope_and_restore(void)
{
    const char *source =
        "q 0 0 80 120 re W n "
        "0 0 1 rg 0 0 120 36 re f "
        "q 20 36 40 48 re 26 50 8 12 re W* n "
        "1 0 0 rg 0 36 120 48 re f Q "
        "0 1 0 rg 0 84 120 36 re f Q "
        "0 0 0 rg 90 0 20 20 re f";
    quantapdf_pdf_page_semantics page;
    quantapdf_ir ir = {0};
    quantapdf_pdf_path_lower_limits limit = lower_budget();
    parse(source, &page);
    CHECK(page.graphics->clip_count == 2 && page.graphics->path_count == 6);
    OK(quantapdf_pdf_path_lower_to_composer(&page, 120, 120, &limit, &ir));
    CHECK(ir.page_count == 1 && ir.operation_count == 4);
    CHECK(ir.resources.clip_count == 3);
    CHECK(ir.resources.clips[0].kind == QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL &&
        ir.resources.clips[0].fill_rule == QUANTAPDF_COMPOSER_FILL_NONZERO &&
        ir.resources.clips[0].command_count == 5);
    CHECK(ir.resources.clips[1].kind == QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL &&
        ir.resources.clips[1].fill_rule == QUANTAPDF_COMPOSER_FILL_EVEN_ODD &&
        ir.resources.clips[1].command_count == 10);
    CHECK(ir.resources.clips[2].kind == QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL &&
        ir.resources.clips[2].member_count == 2);
    CHECK(ir.resources.graphics_state_count == 2);
    CHECK(ir.operations[0].graphics_state_id != 0 &&
        ir.operations[0].graphics_state_id == ir.operations[2].graphics_state_id);
    CHECK(ir.operations[1].graphics_state_id != 0 &&
        ir.operations[1].graphics_state_id != ir.operations[0].graphics_state_id);
    CHECK(ir.operations[3].graphics_state_id == 0);
    CHECK(ir.resources.graphics_states[ir.operations[0].graphics_state_id - 1].clip_id == 1);
    CHECK(ir.resources.graphics_states[ir.operations[1].graphics_state_id - 1].clip_id == 3);
    drop(&page);
    CHECK(ir.resources.clips[1].commands[0].kind == QUANTAPDF_COMPOSER_PATH_MOVE_TO);
    quantapdf_ir_dispose(&ir);
    CHECK(ir.pages == NULL && ir.resources.clips == NULL &&
        ir.resources.graphics_states == NULL);
}

static void test_clip_delayed_after_paint(void)
{
    quantapdf_pdf_page_semantics page;
    quantapdf_ir ir = {0};
    quantapdf_pdf_path_lower_limits limit = lower_budget();
    parse("q 1 0 0 RG 10 w 20 20 40 40 re W S "
          "0 0 1 rg 0 0 120 120 re f Q", &page);
    OK(quantapdf_pdf_path_lower_to_composer(&page, 120, 120, &limit, &ir));
    CHECK(ir.operation_count == 2 && ir.resources.clip_count == 1);
    CHECK(ir.operations[0].graphics_state_id == 0);
    CHECK(ir.operations[0].value.path.options.stroke == 1 &&
        ir.operations[0].value.path.command_count == 5);
    CHECK(ir.operations[1].graphics_state_id != 0);
    CHECK(ir.resources.graphics_states[
        ir.operations[1].graphics_state_id - 1].clip_id == 1);
    quantapdf_ir_dispose(&ir);
    drop(&page);
}

static void test_clip_uses_definition_ctm(void)
{
    quantapdf_pdf_page_semantics page;
    quantapdf_ir ir = {0};
    quantapdf_pdf_path_lower_limits limit = lower_budget();
    parse("q 1 0 0 1 10 0 cm 0 0 60 60 re W n "
          "1 0 0 1 -10 0 cm 0 0 1 rg 0 0 120 120 re f Q", &page);
    OK(quantapdf_pdf_path_lower_to_composer(&page, 120, 120, &limit, &ir));
    CHECK(ir.operation_count == 1 && ir.resources.clip_count == 1);
    CHECK(ir.resources.clips[0].transform.e == 10 &&
        ir.resources.clips[0].transform.f == 120);
    CHECK(ir.operations[0].value.path.options.transform.e == 0 &&
        ir.operations[0].value.path.options.transform.f == 120);
    quantapdf_ir_dispose(&ir);
    drop(&page);
}


/* W/W* with a current MOVE-only path is a valid zero-area Clip node, not
 * the no-constructor W n no-op. The detached IR retains the owned command
 * and a later q/Q-restored paint cannot inherit the empty Clip. */
static void test_move_only_clip_lowering(void)
{
    const char *source[] = {
        "q 12 30 m W n 1 0 0 rg 0 0 120 120 re f Q "
        "0 0 1 rg 20 20 40 40 re f",
        "q 12 30 m W* n 1 0 0 rg 0 0 120 120 re f Q "
        "0 0 1 rg 20 20 40 40 re f"
    };
    size_t variant;
    for (variant = 0u; variant < 2u; ++variant) {
        quantapdf_pdf_page_semantics page;
        quantapdf_ir ir = {0};
        quantapdf_pdf_path_lower_limits limit = lower_budget();
        parse(source[variant], &page);
        CHECK(page.graphics->path_count == 3u &&
              page.graphics->clip_count == 1u);
        OK(quantapdf_pdf_path_lower_to_composer(&page, 120, 120, &limit, &ir));
        CHECK(ir.operation_count == 2u && ir.resources.clip_count == 1u &&
              ir.resources.graphics_state_count == 1u);
        CHECK(ir.resources.clips[0].kind == QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL &&
              ir.resources.clips[0].command_count == 1u &&
              ir.resources.clips[0].commands[0].kind ==
                  QUANTAPDF_COMPOSER_PATH_MOVE_TO &&
              ir.resources.clips[0].fill_rule ==
                  (variant ? QUANTAPDF_COMPOSER_FILL_EVEN_ODD :
                             QUANTAPDF_COMPOSER_FILL_NONZERO));
        CHECK(ir.operations[0].graphics_state_id == 1u &&
              ir.resources.graphics_states[0].clip_id == 1u &&
              ir.operations[1].graphics_state_id == 0u);
        drop(&page);
        CHECK(ir.resources.clips[0].commands[0].point1.x == 12.0f);
        quantapdf_ir_dispose(&ir);
    }

    /* No current path remains the separately specified PDFium-observed
     * W n no-op and must never turn into an empty clipping resource. */
    {
        quantapdf_pdf_page_semantics page;
        quantapdf_ir ir = {0};
        quantapdf_pdf_path_lower_limits limit = lower_budget();
        parse("q W n 1 0 0 rg 0 0 120 120 re f Q", &page);
        CHECK(page.graphics->clip_count == 0u &&
              page.graphics->path_count == 1u);
        OK(quantapdf_pdf_path_lower_to_composer(&page,120,120,&limit,&ir));
        CHECK(ir.resources.clip_count == 0u &&
              ir.operation_count == 1u &&
              ir.operations[0].graphics_state_id == 0u);
        quantapdf_ir_dispose(&ir);
        drop(&page);
    }

    /* A no-segment Fill or W followed by no-segment Fill must not silently
     * acquire the more permissive Clip-only admission contract. */
    {
        const char *negative[] = {
            "12 30 m f",
            "12 30 m W f"
        };
        for (variant = 0u; variant < 2u; ++variant) {
            quantapdf_pdf_page_semantics page;
            quantapdf_ir ir;
            quantapdf_pdf_path_lower_limits limit = lower_budget();
            parse(negative[variant], &page);
            memset(&ir,0xab,sizeof(ir));
            CHECK(quantapdf_pdf_path_lower_to_composer(
                &page,120,120,&limit,&ir) == QUANTAPDF_ERROR_UNSUPPORTED);
            CHECK(ir.pages == NULL && ir.operations == NULL &&
                  ir.resources.clips == NULL && ir.operation_count == 0u);
            drop(&page);
        }
    }
}

static void test_failfast(void)
{
    quantapdf_pdf_page_semantics page;
    quantapdf_ir ir;
    quantapdf_pdf_path_lower_limits limit = lower_budget();
    parse("0 0 10 10 re W n 0 0 120 120 re f", &page);
    limit.max_path_commands = 9;
    memset(&ir, 0xab, sizeof(ir));
    CHECK(quantapdf_pdf_path_lower_to_composer(&page, 120, 120,
        &limit, &ir) == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(ir.page_count == 0 && ir.operation_count == 0 &&
        ir.resources.clip_count == 0 && ir.pages == NULL);
    drop(&page);

    parse("0.12345 0 10 10 re W n 0 0 120 120 re f", &page);
    limit = lower_budget();
    CHECK(quantapdf_pdf_path_lower_to_composer(&page, 120, 120,
        &limit, &ir) == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(ir.resources.clips == NULL && ir.operations == NULL);
    drop(&page);

    parse("0 0 10 10 re W n 0 0 120 120 re f "
          "0 0 120 120 re f", &page);
    limit.max_painted_paths = 1;
    CHECK(quantapdf_pdf_path_lower_to_composer(&page, 120, 120,
        &limit, &ir) == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(ir.pages == NULL);
    drop(&page);

    /* Unused fill state is irrelevant to the geometry of an unpainted clip. */
    parse("0.5 g 0 0 10 10 re W n "
          "0 0 1 rg 0 0 120 120 re f", &page);
    limit = lower_budget();
    OK(quantapdf_pdf_path_lower_to_composer(&page, 120, 120, &limit, &ir));
    quantapdf_ir_dispose(&ir);
    drop(&page);

    /* More clip records than the installed Composer compound clip ABI admits. */
    {
        char source[4096] = "q ";
        for (size_t i = 0; i < QUANTAPDF_COMPOSER_MAX_CLIP_COMPONENTS + 1; ++i)
            strcat(source, "0 0 100 100 re W n ");
        strcat(source, "0 0 120 120 re f Q");
        parse(source, &page);
        limit = lower_budget();
        CHECK(quantapdf_pdf_path_lower_to_composer(&page, 120, 120,
            &limit, &ir) == QUANTAPDF_ERROR_UNSUPPORTED);
        CHECK(ir.resources.clips == NULL && ir.operations == NULL);
        drop(&page);
    }
}

int main(void)
{
    test_nested_clip_scope_and_restore();
    test_clip_delayed_after_paint();
    test_clip_uses_definition_ctm();
    test_move_only_clip_lowering();
    test_failfast();
    return 0;
}
