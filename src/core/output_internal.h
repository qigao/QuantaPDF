#ifndef QUANTAPDF_OUTPUT_INTERNAL_H
#define QUANTAPDF_OUTPUT_INTERNAL_H

#include <quantapdf/quantapdf.h>

struct quantapdf_output {
    quantapdf_context *context;
    unsigned char *data;
    size_t size;
};

#ifdef __cplusplus
extern "C" {
#endif

quantapdf_status quantapdf_output_create_internal(
    quantapdf_context *context, quantapdf_output **out_output);

#ifdef __cplusplus
}
#endif

#endif
