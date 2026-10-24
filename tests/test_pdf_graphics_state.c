#include "ir/pdf_graphics_state.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n", __FILE__,__LINE__,#x); exit(EXIT_FAILURE); } } while (0)
static quantapdf_pdf_content_limits limits(void) { quantapdf_pdf_content_limits v = {4096,128,8}; return v; }

static quantapdf_pdf_graphics_ir *evaluate(const char *source)
{
    quantapdf_pdf_content_ir *tape = NULL;
    quantapdf_pdf_graphics_ir *graphics = NULL;
    quantapdf_pdf_content_limits v = limits();
    CHECK(quantapdf_pdf_content_parse((const unsigned char *)source,strlen(source),&v,&tape)==QUANTAPDF_OK);
    CHECK(quantapdf_pdf_graphics_evaluate(tape,&v,&graphics)==QUANTAPDF_OK);
    quantapdf_pdf_content_dispose(tape);
    return graphics;
}

static void fails(const char *source, quantapdf_status expected)
{
    quantapdf_pdf_content_ir *tape = NULL;
    quantapdf_pdf_graphics_ir *graphics = (quantapdf_pdf_graphics_ir *)(uintptr_t)1;
    quantapdf_pdf_content_limits v=limits();
    CHECK(quantapdf_pdf_content_parse((const unsigned char *)source,strlen(source),&v,&tape)==QUANTAPDF_OK);
    CHECK(quantapdf_pdf_graphics_evaluate(tape,&v,&graphics)==expected);
    CHECK(graphics==NULL);
    quantapdf_pdf_content_dispose(tape);
}

static void test_ctm_and_saved_state(void)
{
    const char *source="q 1 0 0 1 100 0 cm 2 0 0 2 0 0 cm "
        "0.2 0.4 0.6 rg 5 w 0 0 m 10 10 l S Q "
        "0 0 m 4 0 l f";
    quantapdf_pdf_graphics_ir *r=evaluate(source);
    CHECK(r->path_count==2 && r->clip_count==0);
    const quantapdf_pdf_graphics_state *in=&r->paths[0].graphics;
    CHECK(in->ctm[0]==2 && in->ctm[3]==2 && in->ctm[4]==100 && in->ctm[5]==0);
    CHECK(in->fill_rgb[0]>0.199 && in->fill_rgb[2]<0.601 && in->line_width==5);
    const quantapdf_pdf_graphics_state *out=&r->paths[1].graphics;
    CHECK(out->ctm[0]==1 && out->ctm[4]==0 && out->line_width==1);
    CHECK(out->fill_rgb[0]==0 && out->stroke_rgb[2]==0);
    quantapdf_pdf_graphics_dispose(r);
}

static void test_clip_deferred_and_restored(void)
{
    const char *source="q 0 0 20 20 re W* n "
        "0 0 m 1 1 l S "
        "q 0 0 10 10 re W n 0 0 m 2 2 l f Q "
        "0 0 m 3 3 l S Q 0 0 m 4 4 l S";
    quantapdf_pdf_graphics_ir *r=evaluate(source);
    CHECK(r->path_count==6 && r->clip_count==2);
    CHECK(r->paths[0].graphics.clip_id==0 && r->paths[0].clip_after==1);
    CHECK(r->paths[1].graphics.clip_id==1 && r->paths[1].clip_after==1);
    CHECK(r->paths[2].graphics.clip_id==1 && r->paths[2].clip_after==2);
    CHECK(r->paths[3].graphics.clip_id==2);
    CHECK(r->paths[4].graphics.clip_id==1);
    CHECK(r->paths[5].graphics.clip_id==0);
    CHECK(r->clips[0].parent_clip_id==0 && r->clips[0].even_odd==1);
    CHECK(r->clips[1].parent_clip_id==1 && r->clips[1].even_odd==0);
    quantapdf_pdf_graphics_dispose(r);
}

static void test_fail_closed(void)
{
    fails("0 0 m q 1 1 l S Q",QUANTAPDF_ERROR_FORMAT);
    fails("0 0 m 1 w 1 1 l S",QUANTAPDF_ERROR_FORMAT);
    fails("0 0 m W 1 1 l n",QUANTAPDF_ERROR_FORMAT);
    fails("0 0 m W W* n",QUANTAPDF_ERROR_FORMAT);
    fails("0 0 m",QUANTAPDF_ERROR_FORMAT);
    /* Empty current-path Clip is well-formed; W must be terminated by n. */
    fails("W",QUANTAPDF_ERROR_FORMAT);
    fails("1.1 g",QUANTAPDF_ERROR_FORMAT);
    fails("-0.1 0 0 rg",QUANTAPDF_ERROR_FORMAT);
    fails("9999999999999999999999999999999999999999 0 0 9999999999999999999999999999999999999999 0 0 cm "
          "9999999999999999999999999999999999999999 0 0 9999999999999999999999999999999999999999 0 0 cm "
          "9999999999999999999999999999999999999999 0 0 9999999999999999999999999999999999999999 0 0 cm "
          "9999999999999999999999999999999999999999 0 0 9999999999999999999999999999999999999999 0 0 cm "
          "9999999999999999999999999999999999999999 0 0 9999999999999999999999999999999999999999 0 0 cm "
          "9999999999999999999999999999999999999999 0 0 9999999999999999999999999999999999999999 0 0 cm "
          "9999999999999999999999999999999999999999 0 0 9999999999999999999999999999999999999999 0 0 cm "
          "9999999999999999999999999999999999999999 0 0 9999999999999999999999999999999999999999 0 0 cm",
          QUANTAPDF_ERROR_UNSUPPORTED);
}

static void test_standalone_end_path_is_noop(void)
{
    quantapdf_pdf_graphics_ir *r=evaluate("n q 0 0 m 1 1 l S Q n");
    CHECK(r->path_count==1 && r->clip_count==0);
    quantapdf_pdf_graphics_dispose(r);
}

static void test_empty(void)
{
    quantapdf_pdf_graphics_ir *r=evaluate("% comment\n ");
    CHECK(r->path_count==0 && r->clip_count==0);
    quantapdf_pdf_graphics_dispose(r);
    r=(quantapdf_pdf_graphics_ir *)(uintptr_t)1;
    quantapdf_pdf_content_limits b=limits();
    CHECK(quantapdf_pdf_graphics_evaluate(NULL,&b,&r)==QUANTAPDF_ERROR_ARGUMENT);
    CHECK(r==NULL);
}


static void test_stroke_style_graphics_state(void)
{
    const char *source =
        "q 2 J 1 j 8 M [3 1 0 2] 1.25 d "
        "0 0 m 60 0 l S Q "
        "0 0 m 30 0 l S";
    quantapdf_pdf_content_limits budget = limits();
    quantapdf_pdf_content_ir *tape = NULL;
    quantapdf_pdf_graphics_ir *ir = NULL;
    CHECK(quantapdf_pdf_content_parse((const unsigned char *)source,
        strlen(source), &budget, &tape) == QUANTAPDF_OK);
    CHECK(quantapdf_pdf_graphics_evaluate(tape, &budget, &ir) == QUANTAPDF_OK);
    CHECK(ir->path_count == 2 && ir->paths[0].graphics.line_cap == 2);
    CHECK(ir->paths[0].graphics.line_join == 1 &&
        ir->paths[0].graphics.miter_limit == 8.0);
    CHECK(ir->paths[0].graphics.dash_count == 4 &&
        ir->paths[0].graphics.dash_phase == 1.25);
    CHECK(tape->dash_values[ir->paths[0].graphics.dash_offset + 2] == 0);
    CHECK(ir->paths[1].graphics.line_cap == 0 &&
        ir->paths[1].graphics.line_join == 0 &&
        ir->paths[1].graphics.miter_limit == 10.0 &&
        ir->paths[1].graphics.dash_count == 0);
    quantapdf_pdf_graphics_dispose(ir);
    quantapdf_pdf_content_dispose(tape);
}


static void test_resolved_extgstate_scopes(void)
{
    const char *source =
        "/Fill gs q /Stroke gs /Multiply gs 0 0 10 10 re B Q "
        "0 0 10 10 re f";
    quantapdf_pdf_content_limits limit = limits();
    quantapdf_pdf_content_ir *tape = NULL;
    quantapdf_pdf_graphics_ir *ir = NULL;
    quantapdf_pdf_extgstate_delta *deltas;
    CHECK(quantapdf_pdf_content_parse((const unsigned char *)source,
        strlen(source), &limit, &tape) == QUANTAPDF_OK);
    CHECK(quantapdf_pdf_graphics_evaluate(tape, &limit, &ir) ==
        QUANTAPDF_ERROR_UNSUPPORTED && ir == NULL);
    deltas = (quantapdf_pdf_extgstate_delta *)calloc(
        tape->count, sizeof(*deltas));
    CHECK(deltas != NULL);
    deltas[0].resolved = 1;
    deltas[0].presence = QUANTAPDF_PDF_GS_FILL_ALPHA;
    deltas[0].fill_alpha = 0.5;
    deltas[2].resolved = 1;
    deltas[2].presence = QUANTAPDF_PDF_GS_STROKE_ALPHA;
    deltas[2].stroke_alpha = 0.25;
    deltas[3].resolved = 1;
    deltas[3].presence = QUANTAPDF_PDF_GS_BLEND_MODE;
    deltas[3].blend_mode = QUANTAPDF_COMPOSER_BLEND_MULTIPLY;
    CHECK(quantapdf_pdf_graphics_evaluate_resolved(tape, &limit,
        deltas, tape->count, &ir) == QUANTAPDF_OK);
    CHECK(ir->path_count == 2);
    CHECK(ir->paths[0].graphics.fill_alpha == 0.5);
    CHECK(ir->paths[0].graphics.stroke_alpha == 0.25);
    CHECK(ir->paths[0].graphics.blend_mode ==
        QUANTAPDF_COMPOSER_BLEND_MULTIPLY);
    CHECK(ir->paths[1].graphics.fill_alpha == 0.5);
    CHECK(ir->paths[1].graphics.stroke_alpha == 1.0);
    CHECK(ir->paths[1].graphics.blend_mode ==
        QUANTAPDF_COMPOSER_BLEND_NORMAL);
    quantapdf_pdf_graphics_dispose(ir); ir = NULL;
    deltas[2].stroke_alpha = 1.25;
    CHECK(quantapdf_pdf_graphics_evaluate_resolved(tape, &limit,
        deltas, tape->count, &ir) == QUANTAPDF_ERROR_FORMAT);
    CHECK(ir == NULL);
    free(deltas);
    quantapdf_pdf_content_dispose(tape);
}

int main(void)
{
    test_resolved_extgstate_scopes();
    test_stroke_style_graphics_state();
    test_ctm_and_saved_state();
    test_clip_deferred_and_restored();
    test_fail_closed();
    test_standalone_end_path_is_noop();
    test_empty();
    return 0;
}
