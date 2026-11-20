#ifndef QUANTAPDF_BACKEND_QPDF_IMAGE_STREAM_H
#define QUANTAPDF_BACKEND_QPDF_IMAGE_STREAM_H

#include "qpdf_composer.h"

#include <qpdf/QPDFObjectHandle.hh>
#include <vector>

namespace quantapdf::detail {

struct image_stream_error { quantapdf_status status; };

std::vector<unsigned char> qpdf_read_stream(QPDFObjectHandle stream,
    size_t limit, qpdf_stream_decode_level_e level);

bool qpdf_has_dct_filter(QPDFObjectHandle filter);

// The caller has found a DCT filter and bounded the 8-bit Gray/RGB layout.
std::vector<unsigned char> qpdf_decode_dct_image(QPDFObjectHandle image,
    quantapdf_jpeg_layout layout);

} // namespace quantapdf::detail

#endif
