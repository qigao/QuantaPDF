#include "ir.h"

#include <stdlib.h>
#include <string.h>

void quantapdf_ir_dispose(quantapdf_ir *ir)
{
    size_t i;
    if (ir == NULL)
        return;
    for (i = 0u; i < ir->operation_count; ++i) {
        if (ir->operations[i].kind == QUANTAPDF_COMPOSER_OPERATION_TEXT)
            free(ir->operations[i].value.text.text_utf8);
        else if (ir->operations[i].kind ==
                 QUANTAPDF_COMPOSER_OPERATION_PATH) {
            free(ir->operations[i].value.path.dash_lengths);
            free(ir->operations[i].value.path.commands);
        }
        else if (ir->operations[i].kind ==
                 QUANTAPDF_COMPOSER_OPERATION_EMBEDDED_TEXT)
            free(ir->operations[i].value.embedded_text.text_utf8);
        else if (ir->operations[i].kind ==
                 QUANTAPDF_COMPOSER_OPERATION_GLYPH_RUN) {
            free(ir->operations[i].value.glyph_run.glyphs);
            free(ir->operations[i].value.glyph_run.unicode_utf8);
        }
    }
    for (i = 0u; i < ir->image_count; ++i)
        free(ir->images[i].alpha_data);
    for (i = 0u; i < ir->image_count; ++i)
        free(ir->images[i].data);
    for (i = 0u; i < ir->link_count; ++i)
        free(ir->links[i].uri_utf8);
    for (i = 0u; i < ir->outline_count; ++i)
        free(ir->outlines[i].title_utf8);
    for (i = 0u; i < ir->font_count; ++i)
        free(ir->fonts[i].data);
    for (i = 0u; i < ir->paint_count; ++i) {
        free(ir->paints[i].stops);
        free(ir->paints[i].pdf_data);
    }
    for (i = 0u; i < ir->form_count; ++i)
        free(ir->forms[i].pdf_data);
    for (i = 0u; i < ir->clip_count; ++i) {
        free(ir->clips[i].commands);
        free(ir->clips[i].members);
    }
    free(ir->clips);
    free(ir->forms);
    free(ir->paints);
    free(ir->soft_masks);
    free(ir->graphics_states);
    free(ir->fonts);
    free(ir->outlines);
    free(ir->links);
    free(ir->images);
    free(ir->operations);
    free(ir->pages);
    memset(ir, 0, sizeof(*ir));
}
