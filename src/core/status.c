#include <quantapdf/quantapdf.h>

const char *quantapdf_status_string(quantapdf_status status)
{
    switch (status) {
    case QUANTAPDF_OK:
        return "ok";
    case QUANTAPDF_ERROR_ARGUMENT:
        return "invalid argument";
    case QUANTAPDF_ERROR_IO:
        return "I/O error";
    case QUANTAPDF_ERROR_PASSWORD:
        return "password required or invalid";
    case QUANTAPDF_ERROR_FORMAT:
        return "invalid document format";
    case QUANTAPDF_ERROR_UNSUPPORTED:
        return "unsupported operation or content";
    case QUANTAPDF_ERROR_NOMEM:
        return "out of memory";
    case QUANTAPDF_ERROR_BACKEND:
        return "backend error";
    case QUANTAPDF_ERROR_STATE:
        return "invalid state";
    default:
        return "unknown error";
    }
}
