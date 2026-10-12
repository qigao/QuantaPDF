#ifndef QUANTAPDF_QPDF_GRAPH_REPLAY_H
#define QUANTAPDF_QPDF_GRAPH_REPLAY_H

#include "graph/graph.h"
#include <qpdf/QPDF.hh>
#include <vector>

/* Private C++ bridge. The model must already pass structural validation.
 * Returned handles belong to pdf, indexed by local ID minus one. The graph is
 * borrowed only during this call; every provider value/payload is copied.
 * Does not attach a trailer, apply writer policy or decode stream filters. */
struct quantapdf_qpdf_graph_error { quantapdf_status status; };
std::vector<QPDFObjectHandle> quantapdf_qpdf_replay_graph(QPDF& pdf,
    const quantapdf_graph_model& model, size_t max_direct_depth,
    size_t max_direct_visits);

#endif
