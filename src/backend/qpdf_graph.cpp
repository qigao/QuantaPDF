#include "qpdf_graph.h"

#include <qpdf/Pipeline.hh>
#include <qpdf/QPDF.hh>

#include <map>
#include <memory>
#include <algorithm>
#include <cstring>
#include <vector>

namespace {
struct graph_failure { quantapdf_status status; };

void checked(quantapdf_status status)
{
    if (status != QUANTAPDF_OK) throw graph_failure{status};
}

class graph_stream final : public Pipeline {
  public:
    explicit graph_stream(quantapdf_graph *graph) : Pipeline("quantapdf graph stream", nullptr), graph_(graph) {}
    void write(unsigned char const *data, size_t size) override
    {
        if (status == QUANTAPDF_OK) status = quantapdf_graph_append_data(graph_, data, size);
        checked(status);
    }
    void finish() override { finished = true; }
    quantapdf_status status = QUANTAPDF_OK;
    bool finished = false;
  private:
    quantapdf_graph *graph_;
};

struct pending_node { QPDFObjectHandle object; quantapdf_local_id id; size_t depth; bool page_seen = false; };

class importer {
  public:
    importer(quantapdf_graph *graph, quantapdf_graph_limits const& limits) : graph_(graph), limits_(limits) {}

    quantapdf_local_id discover(QPDFObjectHandle object, size_t parent_depth)
    {
        if (object.isIndirect()) {
            auto found = objects_.find(object.getObjGen());
            if (found != objects_.end()) return found->second;
        }
        if (pending_.size() >= limits_.max_nodes || parent_depth >= limits_.max_depth)
            throw graph_failure{QUANTAPDF_ERROR_UNSUPPORTED};
        quantapdf_local_id id = 0;
        checked(quantapdf_graph_reserve_node(graph_, object.isIndirect() ? 1 : 0, &id));
        if (pending_.size() == pending_.capacity()) {
            size_t capacity = pending_.capacity();
            size_t increase = capacity == 0 ? 8 : capacity;
            if (increase > limits_.max_nodes - capacity) increase = limits_.max_nodes - capacity;
            pending_.reserve(capacity + increase);
        }
        pending_.push_back({object, id, parent_depth + 1});
        if (object.isIndirect()) objects_.emplace(object.getObjGen(), id);
        return id;
    }

    void run(QPDF& pdf, quantapdf_validation_report *report)
    {
        quantapdf_local_id trailer = discover(pdf.getTrailer(), 0), catalog = 0;
        for (size_t i = 0; i < pending_.size(); ++i) {
            // Child discovery may reallocate the worklist; retain a value copy.
            pending_node item = pending_[i];
            auto object = item.object;
            quantapdf_graph_kind kind;
            int64_t integer = 0;
            std::string bytes;
            if (object.isNull()) kind = QUANTAPDF_GRAPH_NULL;
            else if (object.isBool()) { kind = QUANTAPDF_GRAPH_BOOL; integer = object.getBoolValue() ? 1 : 0; }
            else if (object.isInteger()) { kind = QUANTAPDF_GRAPH_INTEGER; integer = object.getIntValue(); }
            else if (object.isReal()) { kind = QUANTAPDF_GRAPH_REAL; bytes = object.getRealValue(); }
            else if (object.isName()) { kind = QUANTAPDF_GRAPH_NAME; bytes = object.getName(); }
            else if (object.isString()) { kind = QUANTAPDF_GRAPH_STRING; bytes = object.getStringValue(); }
            else if (object.isArray()) kind = QUANTAPDF_GRAPH_ARRAY;
            else if (object.isDictionary()) kind = QUANTAPDF_GRAPH_DICTIONARY;
            else if (object.isStream()) kind = QUANTAPDF_GRAPH_STREAM;
            else throw graph_failure{QUANTAPDF_ERROR_UNSUPPORTED};
            checked(quantapdf_graph_begin_node(graph_, item.id, kind, integer));
            if (kind == QUANTAPDF_GRAPH_REAL || kind == QUANTAPDF_GRAPH_NAME || kind == QUANTAPDF_GRAPH_STRING)
                checked(quantapdf_graph_append_data(graph_, bytes.data(), bytes.size()));
            if (kind == QUANTAPDF_GRAPH_STREAM) {
                graph_stream sink(graph_);
                // Match QPDF's raw-stream operation. Its filtering-attempted
                // flag may be true for a successful empty stream.
                bool success = object.pipeStreamData(&sink, nullptr, 0, qpdf_dl_none);
                checked(sink.status);
                if (!success || !sink.finished) throw graph_failure{QUANTAPDF_ERROR_FORMAT};
                object = object.getDict();
            }
            if (kind == QUANTAPDF_GRAPH_ARRAY) {
                int count = object.getArrayNItems();
                if (count < 0 || static_cast<size_t>(count) > limits_.max_edges - edges_)
                    throw graph_failure{QUANTAPDF_ERROR_UNSUPPORTED};
                for (int j = 0; j < count; ++j) {
                    auto id = discover(object.getArrayItem(j), item.depth);
                    checked(quantapdf_graph_append_edge(graph_, id, nullptr, 0));
                    ++edges_;
                }
            } else if (kind == QUANTAPDF_GRAPH_DICTIONARY || kind == QUANTAPDF_GRAPH_STREAM) {
                // QPDF's returned map includes explicit null values and orders
                // canonical keys by byte value. Its temporary storage belongs
                // to provider extraction, outside the graph storage budget.
                auto entries = object.getDictAsMap();
                if (entries.size() > limits_.max_edges - edges_)
                    throw graph_failure{QUANTAPDF_ERROR_UNSUPPORTED};
                for (auto const& entry : entries) {
                    auto id = discover(entry.second, item.depth);
                    checked(quantapdf_graph_append_edge(graph_, id, entry.first.data(), entry.first.size()));
                    ++edges_;
                    if (item.id == trailer && entry.first == "/Root") catalog = id;
                }
            }
            checked(quantapdf_graph_end_node(graph_));
        }
        auto page_ids = collect_pages(catalog);
        if (pdf.anyWarnings()) throw graph_failure{QUANTAPDF_ERROR_FORMAT};
        checked(quantapdf_graph_finish(graph_, trailer, catalog, page_ids.data(), page_ids.size(), report));
    }
  private:
    std::vector<quantapdf_local_id> collect_pages(quantapdf_local_id catalog)
    {
        // Read the original hierarchy: getAllPages() repairs resources and
        // inherited attributes, which would change the captured object graph.
        if (catalog == 0 || !pending_[catalog - 1].object.isDictionary())
            throw graph_failure{QUANTAPDF_ERROR_FORMAT};
        auto current = pending_[catalog - 1].object.getKey("/Pages");
        if (!current.isDictionary() || !current.getKey("/Type").isNameAndEquals("/Pages"))
            throw graph_failure{QUANTAPDF_ERROR_FORMAT};
        struct frame { QPDFObjectHandle kids; int next, count; };
        std::vector<frame> stack;
        std::vector<quantapdf_local_id> pages;
        stack.reserve((std::min)(pending_.size(), limits_.max_depth));
        pages.reserve((std::min)(pending_.size(), limits_.max_edges - edges_));
        for (;;) {
            if (!current.isIndirect() || !current.isDictionary())
                throw graph_failure{QUANTAPDF_ERROR_FORMAT};
            auto found = objects_.find(current.getObjGen());
            if (found == objects_.end() || pending_[found->second - 1].page_seen)
                throw graph_failure{QUANTAPDF_ERROR_FORMAT};
            pending_[found->second - 1].page_seen = true;
            if (stack.size() >= limits_.max_depth)
                throw graph_failure{QUANTAPDF_ERROR_UNSUPPORTED};
            auto type = current.getKey("/Type");
            if (type.isNameAndEquals("/Page")) {
                if (pages.size() == limits_.max_edges - edges_)
                    throw graph_failure{QUANTAPDF_ERROR_UNSUPPORTED};
                pages.push_back(found->second);
            } else if (type.isNameAndEquals("/Pages")) {
                auto kids = current.getKey("/Kids");
                if (!kids.isArray()) throw graph_failure{QUANTAPDF_ERROR_FORMAT};
                int count = kids.getArrayNItems();
                if (count < 0) throw graph_failure{QUANTAPDF_ERROR_FORMAT};
                stack.push_back({kids, 0, count});
            } else throw graph_failure{QUANTAPDF_ERROR_FORMAT};
            while (!stack.empty() && stack.back().next == stack.back().count) stack.pop_back();
            if (stack.empty()) return pages;
            auto& parent = stack.back();
            current = parent.kids.getArrayItem(parent.next++);
        }
    }

    quantapdf_graph *graph_;
    quantapdf_graph_limits const& limits_;
    std::vector<pending_node> pending_;
    std::map<QPDFObjGen, quantapdf_local_id> objects_;
    size_t edges_ = 0;
};
}

quantapdf_status quantapdf_qpdf_import_graph(
    QPDF& pdf, const quantapdf_graph_limits *limits, quantapdf_graph **out_graph)
{
    if (out_graph == nullptr) return QUANTAPDF_ERROR_ARGUMENT;
    *out_graph = nullptr;
    quantapdf_graph *raw = nullptr;
    quantapdf_status status = quantapdf_graph_create(limits, &raw);
    if (status != QUANTAPDF_OK) return status;
    std::unique_ptr<quantapdf_graph, decltype(&quantapdf_graph_drop)> graph(raw, quantapdf_graph_drop);
    try {
        if (pdf.anyWarnings()) return QUANTAPDF_ERROR_FORMAT;
        auto version = pdf.getPDFVersion();
        if (version.size() != 3) return QUANTAPDF_ERROR_UNSUPPORTED;
        quantapdf_graph_source source{};
        std::memcpy(source.pdf_version, version.data(), 3);
        source.encrypted = pdf.isEncrypted() ? 1 : 0;
        checked(quantapdf_graph_set_source(graph.get(), &source));
        quantapdf_validation_report report{};
        importer(graph.get(), *limits).run(pdf, &report);
        *out_graph = graph.release();
        return QUANTAPDF_OK;
    } catch (graph_failure const& failure) {
        return failure.status;
    }
}
