#ifndef QUANTAPDF_QPDF_TEXT_H
#define QUANTAPDF_QPDF_TEXT_H

#include <quantapdf/quantapdf.h>

#ifdef __cplusplus
#include <qpdf/QPDFObjectHandle.hh>
#include <array>
#include <memory>
#include <vector>
namespace quantapdf::detail {
struct source_outline_glyph { std::vector<quantapdf_composer_path_command> commands; };
struct source_outline_paint {
    std::vector<source_outline_glyph> glyphs; uint32_t argb;
    std::vector<quantapdf_composer_path_command> rectangle={};
    quantapdf_composer_fill_rule rule=QUANTAPDF_COMPOSER_FILL_NONZERO;
};
bool source_outline_is_rectangle(const std::vector<quantapdf_composer_path_command>&);
struct source_text_seed { std::array<double,6> ctm; uint32_t argb; };
/* Reuses the existing observation interpreter on the SAME token callback
 * stream. It never parses/reopens source bytes or returns font/QPDF owners. */
class source_text_outlines {
public:
    virtual ~source_text_outlines()=default;
    virtual quantapdf_status consume(QPDFObjectHandle)=0;
    virtual quantapdf_status finish()=0;
    virtual const std::vector<source_outline_paint>& paints() const=0;
};
std::unique_ptr<source_text_outlines> make_source_text_outlines(
    QPDFObjectHandle resources,double page_height,source_text_seed initial,
    const std::vector<source_text_seed>& saved,
    const std::vector<source_outline_paint>& prefix={});
}

/* Common conversion/copy body for an already admitted string. Outputs must be
 * non-NULL and reset by the caller. May throw; each C boundary translates its
 * existing provider exceptions. No raw-byte copy is added to document reads. */
quantapdf_status quantapdf_qpdf_copy_text(QPDFObjectHandle value,
    char **out_utf8, size_t *out_size);
extern "C" {
#endif
/* Synchronously borrows explicit PDF string bytes, including embedded NULs.
 * Uses the existing QPDF text-string conversion semantics. Success owns a
 * malloc-allocated, NUL-terminated result (including empty strings); caller
 * releases with quantapdf_free. Failures clear both outputs; no C++ exceptions. */
quantapdf_status quantapdf_qpdf_decode_text(const unsigned char *data, size_t size,
    char **out_utf8, size_t *out_size);
#ifdef __cplusplus
}
#endif
#endif
