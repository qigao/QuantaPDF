#include "qpdf_graph_stream.h"
#include "qpdf_graph_replay.h"

#include <qpdf/Pipeline.hh>
#include <qpdf/QPDFExc.hh>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <new>
#include <stdexcept>

namespace {
struct stream_failure { quantapdf_status status; };
void require(bool ok, quantapdf_status status)
{
    if (!ok) throw stream_failure{status};
}

class decoded_sink final : public Pipeline {
  public:
    explicit decoded_sink(size_t limit) : Pipeline("graph decoded stream", nullptr), limit_(limit) {}
    ~decoded_sink() override { std::free(data_); }
    decoded_sink(decoded_sink const&) = delete;
    decoded_sink& operator=(decoded_sink const&) = delete;
    void write(unsigned char const *data, size_t count) override
    {
        require(!finished_, QUANTAPDF_ERROR_BACKEND);
        require(count <= limit_ - size_, QUANTAPDF_ERROR_UNSUPPORTED);
        size_t needed = size_ + count;
        if (needed > capacity_) {
            size_t growth = capacity_ == 0 ? 4096 : capacity_;
            size_t next = std::max(needed, capacity_ + std::min(growth, limit_ - capacity_));
            auto bytes = static_cast<unsigned char *>(std::realloc(data_, next));
            require(bytes != nullptr, QUANTAPDF_ERROR_NOMEM);
            data_ = bytes; capacity_ = next;
        }
        if (count != 0) std::memcpy(data_ + size_, data, count);
        size_ = needed;
    }
    void finish() override { finished_ = true; }
    void publish(unsigned char **data, size_t *size)
    {
        require(finished_, QUANTAPDF_ERROR_BACKEND);
        *data = data_; *size = size_; data_ = nullptr;
    }
  private:
    unsigned char *data_ = nullptr;
    size_t size_ = 0, capacity_ = 0, limit_;
    bool finished_ = false;
};

bool crypt(quantapdf_graph_model const& model, quantapdf_local_id id)
{
    auto const& node = model.nodes[id - 1];
    return node.kind == QUANTAPDF_GRAPH_NAME && node.data_size == 6 &&
        std::memcmp(model.bytes + node.data_offset, "/Crypt", 6) == 0;
}

void admit_stream(quantapdf_graph_model const& model, quantapdf_local_id id)
{
    require(quantapdf_graph_find(&model, id, "/F", 2) == nullptr &&
        quantapdf_graph_find(&model, id, "/FFilter", 8) == nullptr &&
        quantapdf_graph_find(&model, id, "/FDecodeParms", 13) == nullptr,
        QUANTAPDF_ERROR_UNSUPPORTED);
    auto filter = quantapdf_graph_find(&model, id, "/Filter", 7);
    if (filter == nullptr) return;
    require(!crypt(model, filter->target), QUANTAPDF_ERROR_UNSUPPORTED);
    auto const& value = model.nodes[filter->target - 1];
    if (value.kind == QUANTAPDF_GRAPH_ARRAY)
        for (size_t i = 0; i < value.edge_count; ++i)
            require(!crypt(model, model.edges[value.edge_offset + i].target), QUANTAPDF_ERROR_UNSUPPORTED);
}
}

quantapdf_status quantapdf_qpdf_graph_stream(const quantapdf_graph *graph,
    const quantapdf_graph_reference *reference, const quantapdf_graph_stream_limits *limits,
    unsigned char **out_data, size_t *out_size)
{
    quantapdf_local_id id;
    if (out_data != nullptr) *out_data = nullptr;
    if (out_size != nullptr) *out_size = 0;
    if (graph == nullptr || reference == nullptr || limits == nullptr ||
        out_data == nullptr || out_size == nullptr || limits->max_decoded_bytes == 0 ||
        limits->max_direct_depth == 0 || limits->max_direct_depth > QUANTAPDF_GRAPH_MAX_DIRECT_DEPTH ||
        limits->max_direct_visits == 0) return QUANTAPDF_ERROR_ARGUMENT;
    auto status = quantapdf_graph_resolve(graph, reference, &id);
    if (status != QUANTAPDF_OK) return status;
    auto model = quantapdf_graph_view(graph);
    if (model->nodes[id - 1].kind != QUANTAPDF_GRAPH_STREAM) return QUANTAPDF_ERROR_ARGUMENT;
    try {
        quantapdf_validation_report report{};
        status = quantapdf_validate_graph(model, &report);
        if (status != QUANTAPDF_OK) return status;
        admit_stream(*model, id);
        auto pdf = QPDF::create(); pdf->emptyPDF(); pdf->setSuppressWarnings(true);
        auto values = quantapdf_qpdf_replay_graph(*pdf, *model,
            limits->max_direct_depth, limits->max_direct_visits);
        auto stream = values[id - 1];
        // A null-pipeline query returns filterability. Actual delivery's return
        // value instead means overall success, which can also mean a raw copy.
        bool filterable = stream.pipeStreamData(nullptr, nullptr, 0, qpdf_dl_specialized);
        require(!pdf->anyWarnings(), QUANTAPDF_ERROR_FORMAT);
        require(filterable, QUANTAPDF_ERROR_UNSUPPORTED);
        decoded_sink sink(limits->max_decoded_bytes);
        bool filtered = false;
        bool success = stream.pipeStreamData(&sink, &filtered, 0, qpdf_dl_specialized);
        require(success && filtered && !pdf->anyWarnings(), QUANTAPDF_ERROR_FORMAT);
        sink.publish(out_data, out_size);
        return QUANTAPDF_OK;
    } catch (stream_failure const& failure) { return failure.status; }
      catch (quantapdf_qpdf_graph_error const& failure) { return failure.status; }
      catch (std::bad_alloc const&) { return QUANTAPDF_ERROR_NOMEM; }
      catch (std::length_error const&) { return QUANTAPDF_ERROR_UNSUPPORTED; }
      catch (QPDFExc const&) { return QUANTAPDF_ERROR_FORMAT; }
      catch (std::runtime_error const&) { return QUANTAPDF_ERROR_FORMAT; }
      catch (...) { return QUANTAPDF_ERROR_BACKEND; }
}
