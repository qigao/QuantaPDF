#include "backend/qpdf_graph.h"

#include <qpdf/QPDF.hh>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); std::exit(EXIT_FAILURE); } } while (0)

static quantapdf_graph_limits limits() { return {1000, 3000, 65536, 65536, 262144, 128}; }

static quantapdf_local_id field(quantapdf_graph_model const *m, quantapdf_local_id id, char const *key)
{
    auto edge = quantapdf_graph_find(m, id, key, std::strlen(key));
    CHECK(edge != nullptr); return edge->target;
}

static void test_page_tree()
{
    auto pdf = QPDF::create();
    pdf->emptyPDF();
    auto root = pdf->getRoot().getKey("/Pages");
    auto branch = pdf->makeIndirectObject(QPDFObjectHandle::parse("<< /Type /Pages /Count 2 >>"));
    auto a = pdf->makeIndirectObject(QPDFObjectHandle::parse("<< /Type /Page /Marker 1 >>"));
    auto b = pdf->makeIndirectObject(QPDFObjectHandle::parse("<< /Type /Page /Marker 2 >>"));
    auto c = pdf->makeIndirectObject(QPDFObjectHandle::parse("<< /Type /Page /Marker 3 >>"));
    branch.replaceKey("/Parent", root);
    branch.replaceKey("/Kids", QPDFObjectHandle::newArray({c, a}));
    a.replaceKey("/Parent", branch); c.replaceKey("/Parent", branch); b.replaceKey("/Parent", root);
    root.replaceKey("/Kids", QPDFObjectHandle::newArray({branch, b}));
    root.replaceKey("/Count", QPDFObjectHandle::newInteger(3));
    root.replaceKey("/Resources", QPDFObjectHandle::newDictionary());
    root.replaceKey("/MediaBox", QPDFObjectHandle::parse("[0 0 100 100]"));
    pdf->getRoot().replaceKey("/Values", QPDFObjectHandle::parse("<< /ExplicitNull null /Real +.500 /Name /A#80 /String <0041ff> >>"));
    auto budget = limits();
    quantapdf_graph *graph = nullptr;
    CHECK(quantapdf_qpdf_import_graph(*pdf, &budget, &graph) == QUANTAPDF_OK);
    auto m = quantapdf_graph_view(graph);
    CHECK(m->page_count == 3);
    int const order[] = {3, 1, 2};
    for (size_t i = 0; i < 3; ++i) {
        auto page = m->edges[m->page_offset + i].target;
        CHECK(quantapdf_graph_node_at(m, field(m, page, "/Marker"))->integer == order[i]);
        CHECK(quantapdf_graph_find(m, page, "/Resources", 10) == nullptr);
        CHECK(quantapdf_graph_find(m, page, "/MediaBox", 9) == nullptr);
    }
    auto values = field(m, m->catalog, "/Values");
    CHECK(quantapdf_graph_node_at(m, field(m, values, "/ExplicitNull"))->kind == QUANTAPDF_GRAPH_NULL);
    auto real = quantapdf_graph_node_at(m, field(m, values, "/Real"));
    CHECK(real->kind == QUANTAPDF_GRAPH_REAL && real->data_size == 5);
    CHECK(std::memcmp(m->bytes + real->data_offset, "+.500", 5) == 0);
    auto name = quantapdf_graph_node_at(m, field(m, values, "/Name"));
    CHECK(name->data_size == 3 && std::memcmp(m->bytes + name->data_offset, "/A\x80", 3) == 0);
    auto string = quantapdf_graph_node_at(m, field(m, values, "/String"));
    CHECK(string->data_size == 3 && std::memcmp(m->bytes + string->data_offset, "\0A\xff", 3) == 0);
    auto small = budget; small.max_edges = m->edge_count - 1;
    quantapdf_graph_drop(graph);
    CHECK(quantapdf_qpdf_import_graph(*pdf, &small, &graph) == QUANTAPDF_ERROR_UNSUPPORTED && graph == nullptr);
    // Repeated nodes, including a cycle, are invalid only in the page-tree view.
    branch.replaceKey("/Kids", QPDFObjectHandle::newArray({a, a}));
    CHECK(quantapdf_qpdf_import_graph(*pdf, &budget, &graph) == QUANTAPDF_ERROR_FORMAT && graph == nullptr);
    branch.replaceKey("/Kids", QPDFObjectHandle::newArray({root}));
    CHECK(quantapdf_qpdf_import_graph(*pdf, &budget, &graph) == QUANTAPDF_ERROR_FORMAT && graph == nullptr);
    branch.replaceKey("/Kids", QPDFObjectHandle::newInteger(1));
    CHECK(quantapdf_qpdf_import_graph(*pdf, &budget, &graph) == QUANTAPDF_ERROR_FORMAT && graph == nullptr);
    branch.replaceKey("/Kids", QPDFObjectHandle::newArray({a}));
    a.replaceKey("/Type", QPDFObjectHandle::newName("/Other"));
    CHECK(quantapdf_qpdf_import_graph(*pdf, &budget, &graph) == QUANTAPDF_ERROR_FORMAT && graph == nullptr);
}

static void test_depth_shortcut()
{
    auto pdf = QPDF::create(); pdf->emptyPDF();
    auto current = pdf->getRoot().getKey("/Pages");
    auto all = QPDFObjectHandle::newArray();
    for (int i = 0; i < 12; ++i) {
        auto child = pdf->makeIndirectObject(QPDFObjectHandle::parse("<< /Type /Pages /Count 0 /Kids [] >>"));
        child.replaceKey("/Parent", current);
        current.replaceKey("/Kids", QPDFObjectHandle::newArray({child}));
        all.appendItem(child); current = child;
    }
    pdf->getRoot().replaceKey("/All", all);
    auto budget = limits();
    quantapdf_graph *graph = nullptr;
    CHECK(quantapdf_qpdf_import_graph(*pdf, &budget, &graph) == QUANTAPDF_OK);
    CHECK(quantapdf_graph_view(graph)->page_count == 0); quantapdf_graph_drop(graph);
    budget.max_depth = 5;
    CHECK(quantapdf_qpdf_import_graph(*pdf, &budget, &graph) == QUANTAPDF_ERROR_UNSUPPORTED && graph == nullptr);
}

static void test_encoded_stream()
{
    auto budget = limits();
    quantapdf_graph *graph = nullptr;
    char const encoded[] = "00 41 ff>\n";
    {
        auto pdf = QPDF::create(); pdf->emptyPDF();
        auto stream = pdf->newStream(encoded);
        stream.getDict().replaceKey("/Filter", QPDFObjectHandle::newName("/ASCIIHexDecode"));
        pdf->getRoot().replaceKey("/Encoded", stream);
        pdf->getRoot().replaceKey("/Empty", pdf->newStream(""));
        CHECK(quantapdf_qpdf_import_graph(*pdf, &budget, &graph) == QUANTAPDF_OK);
    }
    auto m = quantapdf_graph_view(graph);
    auto empty = quantapdf_graph_node_at(m, field(m, m->catalog, "/Empty"));
    CHECK(empty->kind == QUANTAPDF_GRAPH_STREAM && empty->data_size == 0);
    auto id = field(m, m->catalog, "/Encoded");
    auto stream = quantapdf_graph_node_at(m, id);
    CHECK(stream->kind == QUANTAPDF_GRAPH_STREAM && stream->data_size == sizeof(encoded) - 1);
    CHECK(std::memcmp(m->bytes + stream->data_offset, encoded, sizeof(encoded) - 1) == 0);
    auto filter = quantapdf_graph_node_at(m, field(m, id, "/Filter"));
    CHECK(filter->kind == QUANTAPDF_GRAPH_NAME && filter->data_size == 15);
    CHECK(std::memcmp(m->bytes + filter->data_offset, "/ASCIIHexDecode", 15) == 0);
    quantapdf_graph_drop(graph);
}

int main() { test_page_tree(); test_depth_shortcut(); test_encoded_stream(); return EXIT_SUCCESS; }
