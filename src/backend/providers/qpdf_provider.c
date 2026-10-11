#include "backend_provider_internal.h"

static quantapdf_status quantapdf_qpdf_open_internal(void *state)
{
    (void)state;
    return QUANTAPDF_STATUS_OK;
}

static void quantapdf_qpdf_close_internal(void *state)
{
    (void)state;
}

static const quantapdf_backend_ops quantapdf_qpdf_ops = {
    .open = quantapdf_qpdf_open_internal,
    .close = quantapdf_qpdf_close_internal,
};

const quantapdf_backend_ops *quantapdf_qpdf_provider_ops_internal(void)
{
    return &quantapdf_qpdf_ops;
}
