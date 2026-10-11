#ifndef QUANTAPDF_QPDF_GRAPH_H
#define QUANTAPDF_QPDF_GRAPH_H

#include "graph/graph.h"

class QPDF;
/* Adapter may throw provider/allocation exceptions; the C bridge translates
 * them. Every failure releases partial graph storage and clears the output. */
quantapdf_status quantapdf_qpdf_import_graph(
    QPDF& pdf, const quantapdf_graph_limits *limits, quantapdf_graph **out_graph);

#endif
