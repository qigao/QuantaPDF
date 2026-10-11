#ifndef QUANTAPDF_VALIDATION_H
#define QUANTAPDF_VALIDATION_H

#include <quantapdf/quantapdf.h>

struct quantapdf_ir;

typedef enum quantapdf_validation_rule {
    QUANTAPDF_VALIDATION_OK = 0,
    QUANTAPDF_VALIDATION_STORAGE,
    QUANTAPDF_VALIDATION_VALUE,
    QUANTAPDF_VALIDATION_REFERENCE,
    QUANTAPDF_VALIDATION_ANCESTRY,
    QUANTAPDF_VALIDATION_SECURITY_POLICY,
    QUANTAPDF_VALIDATION_UNKNOWN_FINDING
} quantapdf_validation_rule;

typedef enum quantapdf_validation_subject {
    QUANTAPDF_VALIDATION_MODEL = 0,
    QUANTAPDF_VALIDATION_PAGE,
    QUANTAPDF_VALIDATION_IMAGE,
    QUANTAPDF_VALIDATION_FONT,
    QUANTAPDF_VALIDATION_GRAPHICS_STATE,
    QUANTAPDF_VALIDATION_SOFT_MASK,
    QUANTAPDF_VALIDATION_CLIP,
    QUANTAPDF_VALIDATION_PAINT,
    QUANTAPDF_VALIDATION_FORM,
    QUANTAPDF_VALIDATION_OPERATION,
    QUANTAPDF_VALIDATION_LINK,
    QUANTAPDF_VALIDATION_OUTLINE,
    QUANTAPDF_VALIDATION_SECURITY
} quantapdf_validation_subject;

/* Caller-owned, first-failure report. index is zero-based; detail is a related
 * ID or security bitmask. No borrowed pointers or hidden allocation. */
typedef struct quantapdf_validation_report {
    quantapdf_validation_rule rule;
    quantapdf_validation_subject subject;
    size_t index;
    size_t detail;
} quantapdf_validation_report;

#ifdef __cplusplus
extern "C" {
#endif

/* Borrows addressable, owned IR storage. Does not parse encoded payloads.
 * Invalid model: BACKEND. NULL arguments: ARGUMENT. Success resets report. */
quantapdf_status quantapdf_validate_ir(
    const struct quantapdf_ir *ir, quantapdf_validation_report *report);

/* Shared public-input and published-path geometry rule; no resource lookup. */
int quantapdf_validation_path_valid(
    const quantapdf_composer_path_command *commands, size_t count);

/* Parsed PDF names, including '/', are borrowed with explicit lengths. */
uint32_t quantapdf_validation_action_finding(const char *name, size_t size);
int quantapdf_validation_rich_media(const char *name, size_t size);

/* Existing policy compliance only, not PDF/A or PDF/UA certification.
 * Forbidden observations: UNSUPPORTED; unknown observations: BACKEND;
 * invalid forbidden mask or NULL report: ARGUMENT. Always resets report. */
quantapdf_status quantapdf_validate_security(
    uint32_t observed, uint32_t forbidden, quantapdf_validation_report *report);

#ifdef __cplusplus
}
#endif

#endif
