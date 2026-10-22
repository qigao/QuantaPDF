#ifndef QUANTAPDF_BACKEND_PDFIUM_DOCUMENT_H
#define QUANTAPDF_BACKEND_PDFIUM_DOCUMENT_H

#include <stddef.h>

#include <quantapdf/quantapdf.h>
#include "document_backend.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct quantapdf_pdfium_document quantapdf_pdfium_document;
typedef struct quantapdf_pdfium_page quantapdf_pdfium_page;
typedef struct quantapdf_text_page quantapdf_text_page;
typedef struct quantapdf_image_page quantapdf_image_page;
typedef struct quantapdf_link_page quantapdf_link_page;

typedef quantapdf_backend_bitmap quantapdf_pdfium_bitmap;

quantapdf_status quantapdf_pdfium_open_memory(
    const unsigned char *data,
    size_t size,
    const char *password_utf8,
    quantapdf_pdfium_document **out_document);

quantapdf_status quantapdf_pdfium_page_count(
    quantapdf_pdfium_document *document,
    int *out_page_count);

quantapdf_status quantapdf_pdfium_load_page(
    quantapdf_pdfium_document *document,
    int page_index,
    quantapdf_pdfium_page **out_page);

quantapdf_status quantapdf_pdfium_page_bounds(
    quantapdf_pdfium_page *page,
    quantapdf_rect *out_bounds);

quantapdf_status quantapdf_pdfium_render_page(
    quantapdf_pdfium_page *page,
    float dpi,
    float rotation_degrees,
    const quantapdf_rect *clip,
    double user_unit,
    int alpha,
    quantapdf_pdfium_bitmap *out_bitmap);

quantapdf_status quantapdf_pdfium_extract_structured_text(
    quantapdf_pdfium_page *page,
    quantapdf_text_page **out_text);

void quantapdf_pdfium_drop_page(quantapdf_pdfium_page *page);
void quantapdf_pdfium_close(quantapdf_pdfium_document *document);

#ifdef __cplusplus
}
#endif

#endif
