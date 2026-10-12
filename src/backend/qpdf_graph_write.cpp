#include "qpdf_graph_write.h"
#include "qpdf_graph_replay.h"

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

bool trailer_bookkeeping(std::string const& key)
{
    return key == "/Size" || key == "/Prev" || key == "/XRefStm" ||
        key == "/Type" || key == "/W" || key == "/Index" || key == "/Length" ||
        key == "/Filter" || key == "/DecodeParms";
}

void attach_trailer(QPDF& pdf, quantapdf_graph_model const& m,
    std::vector<QPDFObjectHandle> const& values)
{
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
        auto pdf = QPDF::create(); pdf->emptyPDF();
        auto values = quantapdf_qpdf_replay_graph(*pdf, *model,
            limits->max_direct_depth, limits->max_direct_visits);
        attach_trailer(*pdf, *model, values);
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
      catch (quantapdf_qpdf_graph_error const& failure) { return failure.status; }
      catch (std::bad_alloc const&) { return QUANTAPDF_ERROR_NOMEM; }
      catch (std::length_error const&) { return QUANTAPDF_ERROR_UNSUPPORTED; }
      catch (...) { return QUANTAPDF_ERROR_BACKEND; }
}
