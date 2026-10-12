#include "qpdf_graph_replay.h"

#include <algorithm>
#include <map>
#include <string>

namespace {
void require(bool ok, quantapdf_status status)
{
    if (!ok) throw quantapdf_qpdf_graph_error{status};
}

/* Postorder follows only inline edges. Indirect references are emitted as
 * references, so their cycles neither recurse nor contribute inline depth. */
std::vector<size_t> direct_order(quantapdf_graph_model const& m,
    size_t max_direct_depth, size_t max_direct_visits)
{
    struct frame { size_t id, next; };
    std::vector<unsigned char> state(m.node_count, 0);
    std::vector<size_t> weights(m.node_count, 1), heights(m.node_count, 1), order;
    std::vector<frame> stack;
    order.reserve(m.node_count); stack.reserve(std::min(m.node_count, max_direct_depth));
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
                    require(stack.size() < max_direct_depth, QUANTAPDF_ERROR_UNSUPPORTED);
                    state[child] = 1; stack.push_back({child, 0});
                    continue;
                }
                size_t weight = m.nodes[child].indirect ? 1 : weights[child];
                require(weight <= max_direct_visits - weights[top.id], QUANTAPDF_ERROR_UNSUPPORTED);
                weights[top.id] += weight;
                if (!m.nodes[child].indirect)
                    heights[top.id] = std::max(heights[top.id], heights[child] + 1);
                require(heights[top.id] <= max_direct_depth, QUANTAPDF_ERROR_UNSUPPORTED);
                ++top.next;
            } else {
                require(weights[top.id] <= max_direct_visits - total, QUANTAPDF_ERROR_UNSUPPORTED);
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

}

std::vector<QPDFObjectHandle> quantapdf_qpdf_replay_graph(QPDF& pdf,
    quantapdf_graph_model const& m, size_t max_direct_depth, size_t max_direct_visits)
{
    require(max_direct_depth != 0 && max_direct_depth <= QUANTAPDF_GRAPH_MAX_DIRECT_DEPTH &&
        max_direct_visits != 0, QUANTAPDF_ERROR_ARGUMENT);
    auto order = direct_order(m, max_direct_depth, max_direct_visits);
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
        default: throw quantapdf_qpdf_graph_error{QUANTAPDF_ERROR_BACKEND};
        }
        if (n.kind == QUANTAPDF_GRAPH_STREAM) values[i].replaceDict(value);
        else if (n.indirect) pdf.replaceReserved(values[i], value);
        else values[i] = value;
    }
    return values;
}
