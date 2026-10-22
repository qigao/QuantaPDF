/* Link-time tripwires for the not-yet-migrated observations. This executable
 * links actual production facade/structure sources, never PDFium. Any attempt
 * to open or use the legacy reader is a hard test failure. */
#include "backend/pdfium_document.h"

#include <stdio.h>
#include <stdlib.h>

static _Noreturn void forbidden(void)
{
    fputs("PDFium called by a migrated document operation\n", stderr);
    abort();
}

quantapdf_status quantapdf_pdfium_open_memory(const unsigned char *data,
    size_t size, const char *password, quantapdf_pdfium_document **out)
{
    (void)data; (void)size; (void)password; (void)out;
    forbidden();
}

quantapdf_status quantapdf_pdfium_load_page(quantapdf_pdfium_document *document,
    int index, quantapdf_pdfium_page **out)
{
    (void)document; (void)index; (void)out;
    forbidden();
}

quantapdf_status quantapdf_pdfium_render_page(quantapdf_pdfium_page *page,
    float dpi, float rotation, const quantapdf_rect *clip, double user_unit,
    int alpha, quantapdf_pdfium_bitmap *out)
{
    (void)page; (void)dpi; (void)rotation; (void)clip; (void)user_unit;
    (void)alpha; (void)out;
    forbidden();
}

quantapdf_status quantapdf_pdfium_extract_structured_text(
    quantapdf_pdfium_page *page, quantapdf_text_page **out)
{
    (void)page; (void)out;
    forbidden();
}

void quantapdf_pdfium_drop_page(quantapdf_pdfium_page *page)
{
    (void)page;
    forbidden();
}

void quantapdf_pdfium_close(quantapdf_pdfium_document *document)
{
    (void)document;
    forbidden();
}
