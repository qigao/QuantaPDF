#ifndef QUANTAPDF_QPDF_TEXT_H
#define QUANTAPDF_QPDF_TEXT_H

#include <quantapdf/quantapdf.h>

#ifdef __cplusplus
class QPDFObjectHandle;
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
