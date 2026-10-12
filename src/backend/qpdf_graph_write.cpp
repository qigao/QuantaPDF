#include "qpdf_graph_write.h"

#include <qpdf/Pipeline.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
struct write_failure { quantapdf_status status; };
void require(bool ok, quantapdf_status status)
{
    if (!ok) throw write_failure{status};
}

class output_sink final : public Pipeline {
  public:
    explicit output_sink(size_t limit) : Pipeline("graph output", nullptr), limit_(limit) {}
    ~output_sink() override { std::free(data_); }
    output_sink(output_sink const&) = delete;
    output_sink& operator=(output_sink const&) = delete;
    void write(unsigned char const *data, size_t count) override
    {
        require(!finished_, QUANTAPDF_ERROR_BACKEND);
        require(count <= limit_ - size_, QUANTAPDF_ERROR_UNSUPPORTED);
        size_t needed = size_ + count;
        if (needed > capacity_) {
            size_t growth = capacity_ == 0 ? 4096 : capacity_;
            size_t next = capacity_ + std::min(growth, limit_ - capacity_);
            next = std::max(next, needed);
            auto buffer = static_cast<unsigned char *>(std::realloc(data_, next));
            require(buffer != nullptr, QUANTAPDF_ERROR_NOMEM);
            data_ = buffer; capacity_ = next;
        }
        if (count != 0) std::memcpy(data_ + size_, data, count);
        size_ = needed;
    }
    void finish() override { finished_ = true; }
    void publish(unsigned char **data, size_t *size)
    {
        require(finished_ && size_ != 0, QUANTAPDF_ERROR_BACKEND);
        *data = data_; *size = size_; data_ = nullptr;
    }
  private:
    unsigned char *data_ = nullptr;
    size_t size_ = 0, capacity_ = 0, limit_;
    bool finished_ = false;
};

bool name(quantapdf_graph_model const& m, quantapdf_local_id id, char const *value)
{
    auto node = quantapdf_graph_node_at(&m, id);
    return node != nullptr && node->kind == QUANTAPDF_GRAPH_NAME &&
        node->data_size == std::strlen(value) &&
        std::memcmp(m.bytes + node->data_offset, value, node->data_size) == 0;
}

bool field_name(quantapdf_graph_model const& m, quantapdf_local_id id,
    char const *key, char const *value)
{
    auto edge = quantapdf_graph_find(&m, id, key, std::strlen(key));
    return edge != nullptr && name(m, edge->target, value);
}

void policy(quantapdf_graph_model const& m)
{
    require(m.source.pdf_version[0] != 0 && !m.source.encrypted &&
        quantapdf_graph_find(&m, m.trailer, "/Encrypt", 8) == nullptr,
        QUANTAPDF_ERROR_UNSUPPORTED);
    for (size_t i = 0; i < m.node_count; ++i) {
        auto id = static_cast<quantapdf_local_id>(i + 1);
        auto const& n = m.nodes[i];
        require(!field_name(m, id, "/Type", "/Sig") && !field_name(m, id, "/FT", "/Sig") &&
            quantapdf_graph_find(&m, id, "/ByteRange", 10) == nullptr,
            QUANTAPDF_ERROR_UNSUPPORTED);
        if (n.kind != QUANTAPDF_GRAPH_STREAM) continue;
        require(n.indirect != 0, QUANTAPDF_ERROR_BACKEND);
        require(quantapdf_graph_find(&m, id, "/F", 2) == nullptr &&
            quantapdf_graph_find(&m, id, "/FFilter", 8) == nullptr &&
            quantapdf_graph_find(&m, id, "/FDecodeParms", 13) == nullptr,
            QUANTAPDF_ERROR_UNSUPPORTED);
        auto filter = quantapdf_graph_find(&m, id, "/Filter", 7);
        if (filter == nullptr) continue;
        require(!name(m, filter->target, "/Crypt"), QUANTAPDF_ERROR_UNSUPPORTED);
        auto const& f = m.nodes[filter->target - 1];
        if (f.kind == QUANTAPDF_GRAPH_ARRAY)
            for (size_t j = 0; j < f.edge_count; ++j)
                require(!name(m, m.edges[f.edge_offset + j].target, "/Crypt"), QUANTAPDF_ERROR_UNSUPPORTED);
    }
}

/* Postorder follows only inline edges. Indirect references are emitted as
 * references, so their cycles neither recurse nor contribute inline depth. */
std::vector<size_t> direct_order(quantapdf_graph_model const& m,
    quantapdf_graph_write_limits const& limits)
{
    struct frame { size_t id, next; };
    std::vector<unsigned char> state(m.node_count, 0);
    std::vector<size_t> weights(m.node_count, 1), heights(m.node_count, 1), order;
    std::vector<frame> stack;
    order.reserve(m.node_count); stack.reserve(std::min(m.node_count, limits.max_direct_depth));
    size_t total = 0;
    for (size_t i = 0; i < m.node_count; ++i) {
        if (state[i] == 2) continue;
        state[i] = 1; stack.push_back({i, 0});
        while (!stack.empty()) {
            auto& top = stack.back();
            auto const& n = m.nodes[top.id];
            if (top.next < n.edge_count) {
                size_t child = m.edges[n.edge_offset + top.next].target - 1;
                if (!m.nodes[child].indirect && state[child] != 2) {
                    require(state[child] == 0, QUANTAPDF_ERROR_BACKEND);
                    require(stack.size() < limits.max_direct_depth, QUANTAPDF_ERROR_UNSUPPORTED);
                    state[child] = 1; stack.push_back({child, 0});
                    continue;
                }
                size_t weight = m.nodes[child].indirect ? 1 : weights[child];
                require(weight <= limits.max_direct_visits - weights[top.id], QUANTAPDF_ERROR_UNSUPPORTED);
                weights[top.id] += weight;
                if (!m.nodes[child].indirect)
                    heights[top.id] = std::max(heights[top.id], heights[child] + 1);
                require(heights[top.id] <= limits.max_direct_depth, QUANTAPDF_ERROR_UNSUPPORTED);
                ++top.next;
            } else {
                require(weights[top.id] <= limits.max_direct_visits - total, QUANTAPDF_ERROR_UNSUPPORTED);
                total += weights[top.id];
                state[top.id] = 2; order.push_back(top.id); stack.pop_back();
            }
        }
    }
    return order;
}

std::string bytes(quantapdf_graph_model const& m, size_t offset, size_t count)
{
    return count == 0 ? std::string{} : std::string(reinterpret_cast<char const *>(m.bytes + offset), count);
}

bool trailer_bookkeeping(std::string const& key)
{
    return key == "/Size" || key == "/Prev" || key == "/XRefStm" ||
        key == "/Type" || key == "/W" || key == "/Index" || key == "/Length" ||
        key == "/Filter" || key == "/DecodeParms";
}

void reconstruct(QPDF& pdf, quantapdf_graph_model const& m, std::vector<size_t> const& order)
{
    std::vector<QPDFObjectHandle> values(m.node_count);
    for (size_t i = 0; i < m.node_count; ++i) {
        auto const& n = m.nodes[i];
        if (n.kind == QUANTAPDF_GRAPH_STREAM)
            values[i] = pdf.newStream(bytes(m, n.data_offset, n.data_size));
        else if (n.indirect) values[i] = pdf.newReserved();
    }
    for (auto i : order) {
        auto const& n = m.nodes[i];
        QPDFObjectHandle value;
        switch (n.kind) {
        case QUANTAPDF_GRAPH_NULL: value = QPDFObjectHandle::newNull(); break;
        case QUANTAPDF_GRAPH_BOOL: value = QPDFObjectHandle::newBool(n.integer != 0); break;
        case QUANTAPDF_GRAPH_INTEGER: value = QPDFObjectHandle::newInteger(n.integer); break;
        case QUANTAPDF_GRAPH_REAL: value = QPDFObjectHandle::newReal(bytes(m, n.data_offset, n.data_size)); break;
        case QUANTAPDF_GRAPH_NAME: value = QPDFObjectHandle::newName(bytes(m, n.data_offset, n.data_size)); break;
        case QUANTAPDF_GRAPH_STRING: value = QPDFObjectHandle::newString(bytes(m, n.data_offset, n.data_size)); break;
        case QUANTAPDF_GRAPH_ARRAY: {
            std::vector<QPDFObjectHandle> items;
            items.reserve(n.edge_count);
            for (size_t j = 0; j < n.edge_count; ++j)
                items.push_back(values[m.edges[n.edge_offset + j].target - 1]);
            value = QPDFObjectHandle::newArray(items); break;
        }
        case QUANTAPDF_GRAPH_DICTIONARY:
        case QUANTAPDF_GRAPH_STREAM: {
            std::map<std::string, QPDFObjectHandle> entries;
            for (size_t j = 0; j < n.edge_count; ++j) {
                auto const& e = m.edges[n.edge_offset + j];
                auto key = bytes(m, e.key_offset, e.key_size);
                if (n.kind == QUANTAPDF_GRAPH_STREAM && key == "/Length") continue;
                entries.emplace(std::move(key), values[e.target - 1]);
            }
            value = QPDFObjectHandle::newDictionary(entries); break;
        }
        default: throw write_failure{QUANTAPDF_ERROR_BACKEND};
        }
        if (n.kind == QUANTAPDF_GRAPH_STREAM) values[i].replaceDict(value);
        else if (n.indirect) pdf.replaceReserved(values[i], value);
        else values[i] = value;
    }
    auto trailer = pdf.getTrailer();
    for (auto const& entry : values[m.trailer - 1].getDictAsMap())
        if (!trailer_bookkeeping(entry.first)) trailer.replaceKey(entry.first, entry.second);
}
}

quantapdf_status quantapdf_qpdf_write_graph(const quantapdf_graph *graph,
    const quantapdf_graph_write_limits *limits, unsigned char **out_data, size_t *out_size)
{
    if (out_data != nullptr) *out_data = nullptr;
    if (out_size != nullptr) *out_size = 0;
    if (graph == nullptr || limits == nullptr || out_data == nullptr || out_size == nullptr ||
        limits->max_output_bytes == 0 || limits->max_direct_visits == 0 ||
        limits->max_direct_depth == 0 || limits->max_direct_depth > QUANTAPDF_GRAPH_WRITE_MAX_DIRECT_DEPTH)
        return QUANTAPDF_ERROR_ARGUMENT;
    auto model = quantapdf_graph_view(graph);
    if (model == nullptr) return QUANTAPDF_ERROR_STATE;
    try {
        quantapdf_validation_report report{};
        auto status = quantapdf_validate_graph(model, &report);
        if (status != QUANTAPDF_OK) return status;
        std::vector<unsigned char> seen(model->node_count);
        std::vector<quantapdf_validation_page_frame> pages(model->node_count);
        status = quantapdf_validate_graph_pages(model, seen.data(), seen.size(), pages.data(), pages.size(), &report);
        if (status != QUANTAPDF_OK) return status;
        policy(*model);
        auto order = direct_order(*model, *limits);
        auto pdf = QPDF::create(); pdf->emptyPDF();
        reconstruct(*pdf, *model, order);
        output_sink sink(limits->max_output_bytes);
        QPDFWriter writer(*pdf);
        writer.setOutputPipeline(&sink);
        writer.setMinimumPDFVersion(model->source.pdf_version);
        writer.setDeterministicID(true);
        writer.setObjectStreamMode(qpdf_o_disable);
        writer.setStreamDataMode(qpdf_s_preserve);
        writer.setPreserveUnreferencedObjects(false);
        writer.write();
        require(!pdf->anyWarnings(), QUANTAPDF_ERROR_BACKEND);
        sink.publish(out_data, out_size);
        return QUANTAPDF_OK;
    } catch (write_failure const& failure) { return failure.status; }
      catch (std::bad_alloc const&) { return QUANTAPDF_ERROR_NOMEM; }
      catch (std::length_error const&) { return QUANTAPDF_ERROR_UNSUPPORTED; }
      catch (...) { return QUANTAPDF_ERROR_BACKEND; }
}
