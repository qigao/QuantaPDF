#include "backend/qpdf_graph.h"
#include "backend/qpdf_graph_stream.h"
#include "graph/graph_edit.h"

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>
#include <zlib.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); std::exit(EXIT_FAILURE); } } while (0)
#define OK(x) CHECK((x) == QUANTAPDF_OK)
using graph_owner = std::unique_ptr<quantapdf_graph, decltype(&quantapdf_graph_drop)>;
static quantapdf_graph_limits graph_limits() { return {10000, 30000, 1048576, 1048576, 4194304, 256}; }
static quantapdf_graph_stream_limits stream_limits() { return {1048576, 128, 1000000}; }

static graph_owner capture(std::string const& encoded, char const *dictionary = "<< >>")
{
    auto pdf = QPDF::create(); pdf->emptyPDF();
    auto stream = pdf->newStream(encoded);
    stream.replaceDict(QPDFObjectHandle::parse(dictionary));
    pdf->getRoot().replaceKey("/Data", stream);
    pdf->getRoot().replaceKey("/Shared", stream);
    quantapdf_graph *graph = nullptr;
    auto limits = graph_limits();
    OK(quantapdf_qpdf_import_graph(*pdf, &limits, &graph));
    return {graph, quantapdf_graph_drop};
}

static quantapdf_graph_reference stream_ref(quantapdf_graph const *graph)
{
    auto model = quantapdf_graph_view(graph);
    auto edge = quantapdf_graph_find(model, model->catalog, "/Data", 5);
    CHECK(edge != nullptr);
    auto shared = quantapdf_graph_find(model, model->catalog, "/Shared", 7);
    CHECK(shared != nullptr && shared->target == edge->target);
    quantapdf_graph_reference reference{};
    OK(quantapdf_graph_reference_at(graph, edge->target, &reference));
    return reference;
}

static void fails(quantapdf_graph const *graph, quantapdf_graph_reference const& reference,
    quantapdf_graph_stream_limits const& limits, quantapdf_status expected)
{
    auto data = reinterpret_cast<unsigned char *>(1); size_t size = 99;
    CHECK(quantapdf_qpdf_graph_stream(graph, &reference, &limits, &data, &size) == expected);
    CHECK(data == nullptr && size == 0);
}

static void check_decode(std::string const& encoded, char const *dictionary, std::string const& expected)
{
    auto graph = capture(encoded, dictionary);
    auto reference = stream_ref(graph.get());
    auto limits = stream_limits();
    limits.max_decoded_bytes = expected.empty() ? 1 : expected.size();
    unsigned char *data = nullptr; size_t size = 0;
    OK(quantapdf_qpdf_graph_stream(graph.get(), &reference, &limits, &data, &size));
    CHECK(size == expected.size());
    auto model = quantapdf_graph_view(graph.get());
    auto node = quantapdf_graph_node_at(model, reference.id);
    CHECK(node->data_size == encoded.size());
    if (!encoded.empty()) CHECK(std::memcmp(model->bytes + node->data_offset, encoded.data(), encoded.size()) == 0);
    if (size > 1) {
        auto small = limits; small.max_decoded_bytes = size - 1;
        fails(graph.get(), reference, small, QUANTAPDF_ERROR_UNSUPPORTED);
    }
    graph.reset();
    if (size == 0) CHECK(data == nullptr);
    else CHECK(data != nullptr && std::memcmp(data, expected.data(), size) == 0);
    std::free(data);
}

static std::string flate(std::string const& source)
{
    uLongf size = compressBound(static_cast<uLong>(source.size()));
    std::string encoded(size, '\0');
    CHECK(compress2(reinterpret_cast<Bytef *>(encoded.data()), &size,
        reinterpret_cast<Bytef const *>(source.data()), static_cast<uLong>(source.size()), Z_BEST_COMPRESSION) == Z_OK);
    encoded.resize(size); return encoded;
}

static void test_codecs_and_limits()
{
    check_decode("", "<< >>", "");
    check_decode(std::string("A\0\xffZ", 4), "<< >>", std::string("A\0\xffZ", 4));
    check_decode("00 41 ff>", "<< /Filter /ASCIIHexDecode >>", std::string("\0A\xff", 3));
    check_decode("z~>", "<< /Filter /ASCII85Decode >>", std::string(4, '\0'));
    check_decode("02 41 42 43 80>", "<< /Filter [/ASCIIHexDecode /RunLengthDecode] >>", "ABC");
    check_decode(flate("q 1 0 0 1 2 3 cm Q\n"), "<< /Filter /FlateDecode >>", "q 1 0 0 1 2 3 cm Q\n");
    check_decode(flate(std::string("\2ABC\2\1\1\1", 8)),
        "<< /Filter /FlateDecode /DecodeParms << /Predictor 12 /Columns 3 /Colors 1 /BitsPerComponent 8 >> >>",
        "ABCBCD");
    check_decode(flate(std::string(10000, 'x')), "<< /Filter /FlateDecode >>", std::string(10000, 'x'));
    check_decode(flate(""), "<< /Filter /FlateDecode >>", "");

    auto graph = capture("abc"); auto reference = stream_ref(graph.get());
    auto limits = stream_limits(); limits.max_direct_depth = 1;
    fails(graph.get(), reference, limits, QUANTAPDF_ERROR_UNSUPPORTED);
    limits = stream_limits(); limits.max_direct_visits = 1;
    fails(graph.get(), reference, limits, QUANTAPDF_ERROR_UNSUPPORTED);
    limits = stream_limits(); limits.max_direct_depth = 0;
    fails(graph.get(), reference, limits, QUANTAPDF_ERROR_ARGUMENT);
    limits.max_direct_depth = QUANTAPDF_GRAPH_MAX_DIRECT_DEPTH + 1;
    fails(graph.get(), reference, limits, QUANTAPDF_ERROR_ARGUMENT);
    limits = stream_limits(); limits.max_decoded_bytes = 0;
    fails(graph.get(), reference, limits, QUANTAPDF_ERROR_ARGUMENT);
    limits = stream_limits(); limits.max_direct_visits = 0;
    fails(graph.get(), reference, limits, QUANTAPDF_ERROR_ARGUMENT);
}

static void test_failures()
{
    struct invalid_case { char const *dictionary, *data; quantapdf_status expected; };
    invalid_case const cases[] = {
        {"<< /Filter /ASCIIHexDecode >>", "GG>", QUANTAPDF_ERROR_FORMAT},
        {"<< /Filter /FlateDecode >>", "invalid", QUANTAPDF_ERROR_FORMAT},
        {"<< /Filter 42 >>", "abc", QUANTAPDF_ERROR_FORMAT},
        {"<< /Filter [/ASCIIHexDecode 42] >>", "abc", QUANTAPDF_ERROR_FORMAT},
        {"<< /Filter [/ASCIIHexDecode /FlateDecode] /DecodeParms [null] >>", "abc", QUANTAPDF_ERROR_FORMAT},
        {"<< /Filter /DCTDecode >>", "abc", QUANTAPDF_ERROR_UNSUPPORTED},
        {"<< /Filter /Unknown >>", "abc", QUANTAPDF_ERROR_UNSUPPORTED},
        {"<< /Filter /FlateDecode /DecodeParms << /Predictor 99 >> >>", "abc", QUANTAPDF_ERROR_UNSUPPORTED},
        {"<< /Filter /Crypt /DecodeParms << /Name /Identity >> >>", "abc", QUANTAPDF_ERROR_UNSUPPORTED},
        {"<< /Filter [/ASCIIHexDecode /Crypt] >>", "abc", QUANTAPDF_ERROR_UNSUPPORTED},
        {"<< /F (unused-external-file) >>", "abc", QUANTAPDF_ERROR_UNSUPPORTED},
        {"<< /FFilter null >>", "abc", QUANTAPDF_ERROR_UNSUPPORTED},
        {"<< /FDecodeParms null >>", "abc", QUANTAPDF_ERROR_UNSUPPORTED}
    };
    for (auto const& item : cases) {
        auto graph = capture(item.data, item.dictionary);
        fails(graph.get(), stream_ref(graph.get()), stream_limits(), item.expected);
    }
    auto graph = capture("abc"), foreign = capture("abc");
    auto reference = stream_ref(graph.get()); auto limits = stream_limits();
    fails(foreign.get(), reference, limits, QUANTAPDF_ERROR_ARGUMENT);
    fails(nullptr, reference, limits, QUANTAPDF_ERROR_ARGUMENT);
    auto model = quantapdf_graph_view(graph.get());
    quantapdf_graph_reference root{};
    OK(quantapdf_graph_reference_at(graph.get(), model->catalog, &root));
    fails(graph.get(), root, limits, QUANTAPDF_ERROR_ARGUMENT);
    quantapdf_graph *unsealed = nullptr; auto budget = graph_limits();
    OK(quantapdf_graph_create(&budget, &unsealed));
    fails(unsealed, reference, limits, QUANTAPDF_ERROR_STATE);
    quantapdf_graph_drop(unsealed);
    auto data = reinterpret_cast<unsigned char *>(1); size_t size = 99;
    CHECK(quantapdf_qpdf_graph_stream(graph.get(), nullptr, &limits, &data, &size) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(data == nullptr && size == 0);
    CHECK(quantapdf_qpdf_graph_stream(graph.get(), &reference, nullptr, &data, &size) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_qpdf_graph_stream(graph.get(), &reference, &limits, nullptr, &size) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_qpdf_graph_stream(graph.get(), &reference, &limits, &data, nullptr) == QUANTAPDF_ERROR_ARGUMENT);
}

static void test_committed_stream()
{
    auto base = capture("41>", "<< /Filter /ASCIIHexDecode >>");
    auto reference = stream_ref(base.get());
    auto model = quantapdf_graph_view(base.get());
    auto node = quantapdf_graph_node_at(model, reference.id);
    std::vector<quantapdf_graph_edge_input> edges;
    for (size_t i = 0; i < node->edge_count; ++i) {
        auto const& edge = model->edges[node->edge_offset + i];
        edges.push_back({edge.target, model->bytes + edge.key_offset, edge.key_size});
    }
    char const encoded[] = "42 00 43>";
    quantapdf_graph_replacement replacement{reference.id,
        {QUANTAPDF_GRAPH_STREAM, 1, 0, encoded, sizeof(encoded) - 1, edges.data(), edges.size()}};
    quantapdf_graph_change_set change{};
    change.lineage = quantapdf_graph_lineage(base.get()); change.base_version = quantapdf_graph_version(base.get());
    change.replacements = &replacement; change.replacement_count = 1;
    quantapdf_graph *next = nullptr; auto budget = graph_limits();
    quantapdf_validation_report report{};
    OK(quantapdf_graph_commit(base.get(), &change, &budget, &next, &report));
    graph_owner owner(next, quantapdf_graph_drop);
    unsigned char *before = nullptr, *after = nullptr; size_t before_size = 0, after_size = 0;
    auto limits = stream_limits();
    OK(quantapdf_qpdf_graph_stream(base.get(), &reference, &limits, &before, &before_size));
    base.reset();
    OK(quantapdf_qpdf_graph_stream(next, &reference, &limits, &after, &after_size));
    owner.reset();
    CHECK(before_size == 1 && before[0] == 'A');
    CHECK(after_size == 3 && std::memcmp(after, "B\0C", 3) == 0);
    std::free(before); std::free(after);
}

static void test_sibling_stream_origin()
{
    auto base = capture("base");
    quantapdf_graph_node_input addition{QUANTAPDF_GRAPH_STREAM, 1, 0, "left", 4, nullptr, 0};
    quantapdf_graph_change_set change{};
    change.lineage = quantapdf_graph_lineage(base.get());
    change.base_version = quantapdf_graph_version(base.get());
    change.additions = &addition; change.addition_count = 1;
    quantapdf_graph *left = nullptr, *right = nullptr;
    auto budget = graph_limits(); quantapdf_validation_report report{};
    OK(quantapdf_graph_commit(base.get(), &change, &budget, &left, &report));
    graph_owner left_owner(left, quantapdf_graph_drop);
    addition.data = "right"; addition.data_size = 5;
    OK(quantapdf_graph_commit(base.get(), &change, &budget, &right, &report));
    graph_owner right_owner(right, quantapdf_graph_drop);
    quantapdf_graph_reference reference{};
    auto id = static_cast<quantapdf_local_id>(quantapdf_graph_view(left)->node_count);
    OK(quantapdf_graph_reference_at(left, id, &reference));
    fails(right, reference, stream_limits(), QUANTAPDF_ERROR_ARGUMENT);
    fails(base.get(), reference, stream_limits(), QUANTAPDF_ERROR_ARGUMENT);
    unsigned char *data = nullptr; size_t size = 0; auto limits = stream_limits();
    OK(quantapdf_qpdf_graph_stream(left, &reference, &limits, &data, &size));
    CHECK(size == 4 && std::memcmp(data, "left", 4) == 0);
    std::free(data);
}

static void test_authenticated_stream()
{
    std::string const expected("authenticated\0bytes", 19);
    std::shared_ptr<Buffer> encrypted;
    {
        auto source = QPDF::create(); source->emptyPDF();
        auto stream = source->newStream(expected);
        source->getRoot().replaceKey("/Data", stream);
        source->getRoot().replaceKey("/Shared", stream);
        QPDFWriter writer(*source); writer.setOutputMemory();
        writer.setR6EncryptionParameters("test-user", "test-owner", true, true,
            true, true, true, true, qpdf_r3p_full, true);
        writer.write(); encrypted = writer.getBufferSharedPointer();
    }
    auto pdf = QPDF::create();
    pdf->processMemoryFile("authenticated stream", reinterpret_cast<char const *>(encrypted->getBuffer()),
        encrypted->getSize(), "test-user");
    quantapdf_graph *graph = nullptr; auto budget = graph_limits();
    OK(quantapdf_qpdf_import_graph(*pdf, &budget, &graph));
    graph_owner owner(graph, quantapdf_graph_drop);
    CHECK(quantapdf_graph_view(graph)->source.encrypted == 1);
    auto reference = stream_ref(graph);
    pdf.reset(); encrypted.reset();
    unsigned char *data = nullptr; size_t size = 0; auto limits = stream_limits();
    OK(quantapdf_qpdf_graph_stream(graph, &reference, &limits, &data, &size));
    owner.reset();
    CHECK(size == expected.size() && std::memcmp(data, expected.data(), size) == 0);
    std::free(data);
}

int main()
{
    test_codecs_and_limits();
    test_failures();
    test_committed_stream();
    test_sibling_stream_origin();
    test_authenticated_stream();
}
