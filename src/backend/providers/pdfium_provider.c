#include "backend_provider_internal.h"

static quantapdf_status quantapdf_pdfium_open_internal(void *state)
{
    (void)state;
    return QUANTAPDF_STATUS_OK;
}

static void quantapdf_pdfium_close_internal(void *state)
{
    (void)state;
}

static const quantapdf_backend_ops quantapdf_pdfium_ops = {
    .open = quantapdf_pdfium_open_internal,
    .close = quantapdf_pdfium_close_internal,
};

const quantapdf_backend_ops *quantapdf_pdfium_provider_ops_internal(void)
{
    return &quantapdf_pdfium_ops;
}
