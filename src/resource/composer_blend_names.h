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
    {"/Lighten", QUANTAPDF_COMPOSER_BLEND_LIGHTEN},
    {"/ColorDodge", QUANTAPDF_COMPOSER_BLEND_COLOR_DODGE},
    {"/ColorBurn", QUANTAPDF_COMPOSER_BLEND_COLOR_BURN},
    {"/HardLight", QUANTAPDF_COMPOSER_BLEND_HARD_LIGHT},
    {"/SoftLight", QUANTAPDF_COMPOSER_BLEND_SOFT_LIGHT},
    {"/Difference", QUANTAPDF_COMPOSER_BLEND_DIFFERENCE},
    {"/Exclusion", QUANTAPDF_COMPOSER_BLEND_EXCLUSION},
    {"/Hue", QUANTAPDF_COMPOSER_BLEND_HUE},
    {"/Saturation", QUANTAPDF_COMPOSER_BLEND_SATURATION},
    {"/Color", QUANTAPDF_COMPOSER_BLEND_COLOR},
    {"/Luminosity", QUANTAPDF_COMPOSER_BLEND_LUMINOSITY}
};

#endif
