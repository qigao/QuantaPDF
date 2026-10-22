#ifndef QUANTAPDF_QPDF_DOCUMENT_INTERNAL_H
#define QUANTAPDF_QPDF_DOCUMENT_INTERNAL_H

/* Private structure-provider state. No renderer or public ABI types escape. */
#include "qpdf_document.h"
#include <qpdf/QPDF.hh>
#include <memory>

struct quantapdf_qpdf_document {
    std::shared_ptr<QPDF> pdf;
    unsigned char const *source_data;
    size_t source_size;
    char const *password;
};

struct quantapdf_qpdf_page_geometry {
    QPDFObjectHandle page;
    double media[4];
    double crop[4];
    double left, bottom, right, top;
    double user_unit, public_width, public_height;
    bool has_explicit_crop;
    int rotation;
};

QPDFObjectHandle quantapdf_qpdf_inherited_value(QPDFObjectHandle page,
    char const *key);
quantapdf_status quantapdf_qpdf_load_page_geometry(QPDF& pdf, int page_index,
    quantapdf_qpdf_page_geometry *out_geometry);
void quantapdf_qpdf_pdf_point_to_public(
    quantapdf_qpdf_page_geometry const& geometry, double raw_x, double raw_y,
    double *out_x, double *out_y);

#endif
