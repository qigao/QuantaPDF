#ifndef QUANTAPDF_COMPOSER_BLEND_NAMES_H
#define QUANTAPDF_COMPOSER_BLEND_NAMES_H

#include "quantapdf/quantapdf.h"

static const struct {
    const char *name;
    quantapdf_composer_blend_mode mode;
} quantapdf_pdf_blend_names[] = {
    {"/Normal", QUANTAPDF_COMPOSER_BLEND_NORMAL},
    {"/Multiply", QUANTAPDF_COMPOSER_BLEND_MULTIPLY},
    {"/Screen", QUANTAPDF_COMPOSER_BLEND_SCREEN},
    {"/Overlay", QUANTAPDF_COMPOSER_BLEND_OVERLAY},
    {"/Darken", QUANTAPDF_COMPOSER_BLEND_DARKEN},
    {"/Lighten", QUANTAPDF_COMPOSER_BLEND_LIGHTEN}
};

#endif
