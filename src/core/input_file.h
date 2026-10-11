#ifndef QUANTAPDF_INPUT_FILE_H
#define QUANTAPDF_INPUT_FILE_H

#include <stddef.h>

#include <quantapdf/quantapdf.h>

quantapdf_status quantapdf_read_file(
    const char *filename,
    unsigned char **out_data,
    size_t *out_size);

#endif
