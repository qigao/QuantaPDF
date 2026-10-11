#include "../src/document/document_internal.h"
#include "pdf_image_recompression_test_api.h"

#include <string.h>

void quantapdf_test_image_recompression_get_stats(
    quantapdf_document *document,
    quantapdf_test_image_recompression_stats *out_stats)
{
    if (out_stats == NULL)
        return;
    memset(out_stats, 0, sizeof(*out_stats));
#if defined(QUANTAPDF_TESTING)
    if (document != NULL) {
        out_stats->unique_images = document->test_image_unique_count;
        out_stats->provider_registrations =
            document->test_image_provider_registrations;
        out_stats->provider_invocations =
            document->test_image_provider_invocations;
        out_stats->decoded_preflight_bytes =
            document->test_image_decoded_preflight_bytes;
        out_stats->every_provider_once =
            document->test_image_every_provider_once;
    }
#else
    (void)document;
#endif
}

void quantapdf_test_image_recompression_set_fault(
    quantapdf_document *document,
    quantapdf_test_image_recompression_fault fault)
{
#if defined(QUANTAPDF_TESTING)
    if (document != NULL)
        document->test_image_fault = (int)fault;
#else
    (void)document;
    (void)fault;
#endif
}
