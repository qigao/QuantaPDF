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
struct source_text_seed {
    std::array<double,6> ctm;
    uint32_t argb;
    /* PRIVATE Source W/W* identity at the original BT handoff; never
     * observed by ordinary extraction, which defaults to GS0. */
    unsigned char clip_id=0u;
};
/* M2a private callbacks: complete original Source token stream, bounded
 * Text outline interpreter, no second parser or retained QPDF/font owner. */
struct source_mixed_callbacks {
    void *user=nullptr;
    quantapdf_status (*on_text)(void*)=nullptr;
    quantapdf_status (*on_image)(void*,QPDFObjectHandle,
                                 const std::array<double,6>&)=nullptr;
    /* Original post-BT opaque solid Fill: the SAME reader freezes the
     * PATH paint alongside Text shows, rather than ignoring its order.
     * Existing Text-only paints do not need a new IR materializer. */
    quantapdf_status (*on_fill)(void*)=nullptr;
    /* PRIVATE M2b.2e: q/Q in the SAME original token interpreter,
     * with actual saved/restored Clip identity (not a later page flag).
     * is_save=1 for q, 0 for Q. Historical callers leave this NULL. */
    quantapdf_status (*on_scope)(void*,int is_save,
                                  unsigned char before,
                                  unsigned char after)=nullptr;
};
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
    const std::vector<source_outline_paint>& prefix={},
    source_mixed_callbacks callbacks={});
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
