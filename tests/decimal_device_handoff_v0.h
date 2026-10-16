#ifndef QUANTAPDF_TEST_DECIMAL_DEVICE_HANDOFF_V0_H
#define QUANTAPDF_TEST_DECIMAL_DEVICE_HANDOFF_V0_H

#include "graph/graph_page_device.h"
#include "decimal_graph_exact_v0.h"

#ifdef __cplusplus
extern "C" {
#endif

/* #360 TEST-ONLY Graph-owner-time device-grid handoff, not public/installed SDK.
 *
 * Called while a sealed Graph snapshot is alive. Calculates bounded exact
 * integer pixel extents from the Graph's original decimal tokens, then
 * constructs the ORIGINAL unmodified Graph display affine using those
 * integer extents as the bitmap-fit scaling inputs. The six affine doubles
 * are observations, not a new Fill/Clip pixel-parity claim.
 *
 * Output owns its values (dimensions, bytes, six doubles) and borrows
 * NOTHING from Graph/source/provider/DSO. It remains valid after all source
 * documents and the snapshot are dropped. Every failure fully zeros output,
 * does not allocate a bitmap, and never falls back to another provider.
 */
quantapdf_status quantapdf_test_graph_device_handoff_v0(
    const quantapdf_graph *graph, size_t page_index,
    size_t max_parent_steps, unsigned dpi, size_t max_pixels,
    size_t max_output_bytes, quantapdf_graph_device_grid *out_grid);

#ifdef __cplusplus
}
#endif

#endif
