#include "backend/qpdf_graph.h"
#include "backend/qpdf_graph_write.h"
#include "graph/graph_edit.h"

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); std::exit(EXIT_FAILURE); } } while (0)
#define OK(x) CHECK((x) == QUANTAPDF_OK)
using graph_owner = std::unique_ptr<quantapdf_graph, decltype(&quantapdf_graph_drop)>;
static quantapdf_graph_limits graph_limits() { return {10000, 30000, 1048576, 1048576, 4194304, 256}; }
static quantapdf_graph_write_limits write_limits() { return {1048576, 128, 1000000}; }

static graph_owner capture(QPDF& pdf)
{
    quantapdf_graph *graph = nullptr;
    auto limits = graph_limits();
    OK(quantapdf_qpdf_import_graph(pdf, &limits, &graph));
    return {graph, quantapdf_graph_drop};
}

static quantapdf_graph_change_set select(quantapdf_graph const *graph)
{
    quantapdf_graph_change_set changes{};
    changes.lineage = quantapdf_graph_lineage(graph);
    changes.base_version = quantapdf_graph_version(graph);
    return changes;
}

static graph_owner commit(quantapdf_graph const *graph, quantapdf_graph_change_set const& changes)
{
    quantapdf_graph *result = nullptr;
    quantapdf_validation_report report{};
    auto limits = graph_limits();
    OK(quantapdf_graph_commit(graph, &changes, &limits, &result, &report));
    return {result, quantapdf_graph_drop};
}

static std::vector<unsigned char> write(quantapdf_graph const *graph)
{
    unsigned char *data = nullptr; size_t size = 0;
    auto limits = write_limits();
    OK(quantapdf_qpdf_write_graph(graph, &limits, &data, &size));
    CHECK(data != nullptr && size > 0);
    std::vector<unsigned char> result(data, data + size); std::free(data);
    return result;
}

static void fails(quantapdf_graph const *graph, quantapdf_graph_write_limits const& limits,
    quantapdf_status expected)
{
    auto data = reinterpret_cast<unsigned char *>(1); size_t size = 99;
    CHECK(quantapdf_qpdf_write_graph(graph, &limits, &data, &size) == expected);
    CHECK(data == nullptr && size == 0);
}

static quantapdf_local_id field(quantapdf_graph_model const *m, quantapdf_local_id id, char const *key)
{
    auto edge = quantapdf_graph_find(m, id, key, std::strlen(key)); CHECK(edge != nullptr); return edge->target;
}

static void test_values_and_limits()
{
    auto pdf = QPDF::create(); pdf->emptyPDF();
    auto root = pdf->getRoot();
    root.replaceKey("/Version", QPDFObjectHandle::newName("/2.0"));
    root.replaceKey("/Extensions", QPDFObjectHandle::parse("<< /ADBE << /BaseVersion /1.7 /ExtensionLevel 8 >> >>"));
    auto shared = pdf->makeIndirectObject(QPDFObjectHandle::newDictionary());
    shared.replaceKey("/Self", shared);
    root.replaceKey("/A", shared); root.replaceKey("/B", shared);
    root.replaceKey("/Values", QPDFObjectHandle::parse("<< /Real +.500 /Name /A#80 /Binary <0041ff> /Null null >>"));
    auto stream = pdf->newStream("00 41 ff>\n");
    stream.getDict().replaceKey("/Filter", QPDFObjectHandle::newName("/ASCIIHexDecode"));
    root.replaceKey("/Encoded", stream); root.replaceKey("/Empty", pdf->newStream(""));
    auto base = capture(*pdf); pdf.reset();
    auto changes = select(base.get());
    auto graph = commit(base.get(), changes); base.reset();
    auto first = write(graph.get()), second = write(graph.get());
    CHECK(first == second);
    auto limits = write_limits(); limits.max_output_bytes = first.size() - 1;
    fails(graph.get(), limits, QUANTAPDF_ERROR_UNSUPPORTED);
    limits.max_output_bytes = first.size();
    unsigned char *exact = nullptr; size_t count = 0;
    OK(quantapdf_qpdf_write_graph(graph.get(), &limits, &exact, &count));
    CHECK(count == first.size() && std::memcmp(exact, first.data(), count) == 0); std::free(exact);
    limits = write_limits(); limits.max_direct_depth = 1;
    fails(graph.get(), limits, QUANTAPDF_ERROR_UNSUPPORTED);
    limits = write_limits(); limits.max_direct_visits = 1;
    fails(graph.get(), limits, QUANTAPDF_ERROR_UNSUPPORTED);
    limits = write_limits(); limits.max_direct_depth = 129;
    fails(graph.get(), limits, QUANTAPDF_ERROR_ARGUMENT);

    auto reopened = QPDF::create();
    reopened->processMemoryFile("graph output", reinterpret_cast<char const *>(first.data()), first.size());
    CHECK(!reopened->anyWarnings());
    root = reopened->getRoot();
    CHECK(root.getKey("/Version").getName() == "/2.0");
    CHECK(root.getKey("/Extensions").getKey("/ADBE").getKey("/ExtensionLevel").getIntValue() == 8);
    CHECK(root.getKey("/A").getObjGen() == root.getKey("/B").getObjGen());
    CHECK(root.getKey("/A").getKey("/Self").getObjGen() == root.getKey("/A").getObjGen());
    CHECK(root.getKey("/Values").getKey("/Binary").getStringValue() == std::string("\0A\xff", 3));
    CHECK(root.getKey("/Values").getKey("/Name").getName() == "/A\x80");
    CHECK(root.getKey("/Values").getKey("/Real").getRealValue() == "+.500");
    CHECK(root.getKey("/Encoded").getDict().getKey("/Filter").getName() == "/ASCIIHexDecode");
    auto encoded = root.getKey("/Encoded").getRawStreamData();
    CHECK(encoded->getSize() == 10 && std::memcmp(encoded->getBuffer(), "00 41 ff>\n", 10) == 0);
    CHECK(root.getKey("/Empty").getRawStreamData()->getSize() == 0);

    /* The source header itself survives, independently of /Catalog /Version. */
    std::vector<unsigned char> versioned = first;
    CHECK(versioned.size() > 8); versioned[5] = '2'; versioned[7] = '0';
    auto version_pdf = QPDF::create();
    version_pdf->processMemoryFile("version", reinterpret_cast<char const *>(versioned.data()), versioned.size());
    auto version_graph = capture(*version_pdf);
    CHECK(std::strcmp(quantapdf_graph_view(version_graph.get())->source.pdf_version, "2.0") == 0);
    auto output = write(version_graph.get());
    CHECK(std::memcmp(output.data(), "%PDF-2.0", 8) == 0);
}

static void test_direct_structure()
{
    auto pdf = QPDF::create(); pdf->emptyPDF(); auto base = capture(*pdf);
    auto count = quantapdf_graph_view(base.get())->node_count;
    auto id = static_cast<quantapdf_local_id>(count + 1);
    quantapdf_graph_edge_input edge{id, nullptr, 0};
    quantapdf_graph_node_input node{QUANTAPDF_GRAPH_ARRAY, 0, 0, nullptr, 0, &edge, 1};
    auto changes = select(base.get()); changes.additions = &node; changes.addition_count = 1;
    auto cyclic = commit(base.get(), changes);
    fails(cyclic.get(), write_limits(), QUANTAPDF_ERROR_BACKEND);
    node.indirect = 1;
    auto indirect = commit(base.get(), changes); CHECK(!write(indirect.get()).empty());

    std::vector<quantapdf_graph_node_input> nodes(12);
    std::vector<quantapdf_graph_edge_input> edges(22);
    for (size_t i = 0; i < nodes.size(); ++i) {
        nodes[i].kind = QUANTAPDF_GRAPH_ARRAY;
        if (i + 1 == nodes.size()) continue;
        edges[i * 2] = edges[i * 2 + 1] = {static_cast<quantapdf_local_id>(id + i + 1), nullptr, 0};
        nodes[i].edges = &edges[i * 2]; nodes[i].edge_count = 2;
    }
    changes.additions = nodes.data(); changes.addition_count = nodes.size();
    auto dag = commit(base.get(), changes);
    auto limits = write_limits(); limits.max_direct_visits = 100;
    fails(dag.get(), limits, QUANTAPDF_ERROR_UNSUPPORTED);
    limits = write_limits(); limits.max_direct_depth = 11;
    fails(dag.get(), limits, QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(!write(dag.get()).empty()); CHECK(!write(base.get()).empty());
}

static void test_policy()
{
    auto pdf = QPDF::create(); pdf->processFile(GRAPH_ENCRYPTED_PDF, "user-pass");
    auto graph = capture(*pdf); pdf.reset();
    auto m = quantapdf_graph_view(graph.get()); CHECK(m->source.encrypted == 1);
    fails(graph.get(), write_limits(), QUANTAPDF_ERROR_UNSUPPORTED);
    std::vector<quantapdf_graph_edge_input> edges;
    auto const& trailer = m->nodes[m->trailer - 1];
    for (size_t i = 0; i < trailer.edge_count; ++i) {
        auto const& e = m->edges[trailer.edge_offset + i];
        if (e.key_size == 8 && std::memcmp(m->bytes + e.key_offset, "/Encrypt", 8) == 0) continue;
        edges.push_back({e.target, m->bytes + e.key_offset, e.key_size});
    }
    quantapdf_graph_replacement replacement{m->trailer,
        {QUANTAPDF_GRAPH_DICTIONARY, trailer.indirect, 0, nullptr, 0, edges.data(), edges.size()}};
    auto changes = select(graph.get()); changes.replacements = &replacement; changes.replacement_count = 1;
    auto stripped = commit(graph.get(), changes);
    CHECK(quantapdf_graph_view(stripped.get())->source.encrypted == 1);
    fails(stripped.get(), write_limits(), QUANTAPDF_ERROR_UNSUPPORTED);

    pdf = QPDF::create(); pdf->emptyPDF();
    pdf->getRoot().replaceKey("/Signature", pdf->makeIndirectObject(QPDFObjectHandle::parse("<< /Type /Sig >>")));
    auto signature = capture(*pdf); fails(signature.get(), write_limits(), QUANTAPDF_ERROR_UNSUPPORTED);
    pdf->getRoot().removeKey("/Signature");
    auto stream = pdf->newStream("");
    stream.getDict().replaceKey("/Filter", QPDFObjectHandle::newName("/ASCIIHexDecode"));
    pdf->getRoot().replaceKey("/Stream", stream);
    auto plain = capture(*pdf); m = quantapdf_graph_view(plain.get());
    auto filter = field(m, field(m, m->catalog, "/Stream"), "/Filter");
    replacement = {filter, {QUANTAPDF_GRAPH_NAME, 0, 0, "/Crypt", 6, nullptr, 0}};
    changes = select(plain.get()); changes.replacements = &replacement; changes.replacement_count = 1;
    auto crypt = commit(plain.get(), changes); fails(crypt.get(), write_limits(), QUANTAPDF_ERROR_UNSUPPORTED);
    auto stream_id = field(m, m->catalog, "/Stream");
    quantapdf_graph_edge_input external[] = {{filter, "/F", 2}};
    replacement = {stream_id, {QUANTAPDF_GRAPH_STREAM, 1, 0, nullptr, 0, external, 1}};
    changes.replacements = &replacement;
    auto file = commit(plain.get(), changes); fails(file.get(), write_limits(), QUANTAPDF_ERROR_UNSUPPORTED);
}

static void test_page_commit_output()
{
    auto pdf = QPDF::create(); pdf->processFile(GRAPH_PDF);
    auto base = capture(*pdf); pdf.reset();
    auto m = quantapdf_graph_view(base.get()); CHECK(m->page_count == 3);
    quantapdf_local_id pages[3]; quantapdf_graph_edge_input kids[3];
    for (size_t i = 0; i < 3; ++i) {
        pages[i] = m->edges[m->page_offset + 2 - i].target;
        kids[i] = {pages[i], nullptr, 0};
    }
    auto kids_id = field(m, field(m, m->catalog, "/Pages"), "/Kids");
    quantapdf_graph_replacement replacement{kids_id,
        {QUANTAPDF_GRAPH_ARRAY, m->nodes[kids_id - 1].indirect, 0, nullptr, 0, kids, 3}};
    auto changes = select(base.get()); changes.replacements = &replacement; changes.replacement_count = 1;
    changes.replace_roots = 1; changes.trailer = m->trailer; changes.catalog = m->catalog;
    changes.pages = pages; changes.page_count = 3;
    auto graph = commit(base.get(), changes);
    auto original = write(base.get()); auto reordered = write(graph.get());
    base.reset(); graph.reset();
    for (int pass = 0; pass < 2; ++pass) {
        auto const& data = pass == 0 ? original : reordered;
        auto opened = QPDF::create();
        opened->processMemoryFile("pages", reinterpret_cast<char const *>(data.data()), data.size());
        auto const& result = opened->getAllPages(); CHECK(result.size() == 3);
        QPDFObjGen font;
        for (size_t i = 0; i < 3; ++i) {
            auto page = result[i];
            auto content = page.getKey("/Contents").getStreamData();
            std::string text(reinterpret_cast<char const *>(content->getBuffer()), content->getSize());
            auto marker = std::string("PAGE-") + static_cast<char>('A' + (pass == 0 ? i : 2 - i));
            CHECK(text.find(marker) != std::string::npos);
            auto current = page.getKey("/Resources").getKey("/Font").getKey("/F1").getObjGen();
            if (i == 0) font = current; else CHECK(current == font);
        }
        CHECK(!opened->anyWarnings());
    }
}

int main() { test_values_and_limits(); test_direct_structure(); test_policy(); test_page_commit_output(); return EXIT_SUCCESS; }
