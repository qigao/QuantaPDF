#ifndef QUANTAPDF_QPDF_TEXT_H
#define QUANTAPDF_QPDF_TEXT_H

#include <quantapdf/quantapdf.h>

#ifdef __cplusplus
#include <qpdf/QPDFObjectHandle.hh>
#include <array>
#include <memory>
#include <string>
#include <vector>
namespace quantapdf::detail {
struct source_outline_glyph { std::vector<quantapdf_composer_path_command> commands; };
struct source_outline_paint {
    std::vector<source_outline_glyph> glyphs; uint32_t argb;
};
struct source_text_seed {
    std::array<double,6> ctm;
    uint32_t argb;
    /* PRIVATE Source W/W* identity at the original BT handoff; never
     * observed by ordinary extraction, which defaults to GS0. */
    quantapdf_composer_clip_id clip_id=0u;
};
/* Page-scene callbacks on the original qpdf token stream. Fonts and
 * contours use the existing interpreter; qpdf/font owners do not escape. */
struct source_mixed_callbacks {
    void *user=nullptr;
    quantapdf_status (*on_text)(void*,quantapdf_composer_clip_id)=nullptr;
    quantapdf_status (*on_image)(void*,QPDFObjectHandle,
        const std::array<double,6>&,quantapdf_composer_clip_id)=nullptr;
    /* Ordinary page paths retain one owner across BT. Operands are passed
     * directly once; the page reader freezes the supplied current state and
     * checks that BT does not enter an unfinished path object. */
    quantapdf_status (*on_path)(void*,const std::string&,
        const std::vector<QPDFObjectHandle>&,source_text_seed,
        quantapdf_composer_clip_id *after)=nullptr;
};
/* Reuses the existing observation interpreter on the SAME token callback
 * stream. It never parses/reopens source bytes or returns font/QPDF owners.
 * The caller supplies an empty owner; construction failures preserve their
 * status and leave it empty, just like consume/finish. */
class source_text_outlines {
public:
    virtual ~source_text_outlines()=default;
    virtual quantapdf_status consume(QPDFObjectHandle)=0;
    virtual quantapdf_status finish()=0;
    virtual const std::vector<source_outline_paint>& paints() const=0;
};
quantapdf_status make_source_text_outlines(
    QPDFObjectHandle resources,double page_height,source_text_seed initial,
    const std::vector<source_text_seed>& saved,
    source_mixed_callbacks callbacks,std::unique_ptr<source_text_outlines>& out);
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
