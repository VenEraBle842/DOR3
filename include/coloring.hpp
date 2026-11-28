#pragma once

#include "graph.hpp"
#include "sequence_heap_sort.hpp"
#include <limits>
#include <optional>
#include <functional>
#include <string_view>

enum class ColoringAlgorithm {
#define DOR3_COLORING(id, displayName, implementation) id,
#include "coloring_catalog.def"
#undef DOR3_COLORING
    Count
};

inline std::optional<std::size_t> coloringVertexPosition(const ArraySequence<VertexId>& ids, VertexId id) {
    std::size_t first = 0, last = ids.size();
    while (first < last) {
        const auto middle = first + (last - first) / 2;
        if (ids[middle] < id) {
            first = middle + 1;
        }
        else last = middle;
    }
    if (first == ids.size() || ids[first] != id) {
        return std::nullopt;
    }
    return first;
}

struct ColoringResult {
    ArraySequence<VertexId> vertexIds;
    ArraySequence<std::size_t> colors;
    ArraySequence<VertexId> processingOrder;
    std::size_t colorCount = 0;

    Result<std::size_t> color(VertexId id) const {
        auto position = coloringVertexPosition(vertexIds, id);
        if (!position || *position >= colors.size()) {
            return failure(GraphErrorCode::InvalidVertex);
        }
        return colors[*position];
    }
};

template <class TGraph>
concept ColorableGraph = requires(const TGraph& graph, VertexId id) {
    { graph.vertexCount() } -> std::convertible_to<std::size_t>;
    { graph.vertexIds() } -> std::same_as<ArraySequence<VertexId>>;
    { graph.neighbors(id) } -> std::same_as<Result<std::reference_wrapper<const ArraySequence<AdjacencyEntry>>>>;
};

template <ColorableGraph TGraph>
Result<ColoringResult> colorGraphGreedily(const TGraph& graph, ArraySequence<VertexId> processingOrder) {
    const auto count = graph.vertexCount();
    if (processingOrder.size() != count) {
        return failure(GraphErrorCode::InvalidParameters, "Order must contain every vertex once.");
    }
    ColoringResult result;
    result.vertexIds = graph.vertexIds();
    ArraySequence<bool> seenVertices(count, false);
    for (const auto id : processingOrder) {
        auto position = coloringVertexPosition(result.vertexIds, id);
        if (!position || seenVertices[*position]) {
            return failure(GraphErrorCode::InvalidParameters, "Order must contain every vertex once.");
        }
        seenVertices[*position] = true;
    }
    constexpr auto uncolored = std::numeric_limits<std::size_t>::max();
    result.processingOrder = std::move(processingOrder);
    result.colors = ArraySequence<std::size_t>(count, uncolored);
    ArraySequence<std::size_t> marks(count, uncolored);
    for (std::size_t step = 0; step < count; ++step) {
        const auto vertex = result.processingOrder[step];
        const auto& neighbors = graph.neighbors(vertex)->get();
        for (const auto& neighbor : neighbors) {
            const auto color = result.colors[*coloringVertexPosition(result.vertexIds, neighbor.neighbor)];
            if (color != uncolored) {
                marks[color] = step;
            }
        }
        std::size_t color = 0;
        while (color < count && marks[color] == step) {
            ++color;
        }
        result.colors[*coloringVertexPosition(result.vertexIds, vertex)] = color;
        if (color + 1 > result.colorCount) {
            result.colorCount = color + 1;
        }
    }
    return result;
}

template <ColorableGraph TGraph>
Result<ColoringResult> colorGraphSequentially(const TGraph& graph) {
    return colorGraphGreedily(graph, graph.vertexIds());
}

template <ColorableGraph TGraph>
Result<ColoringResult> colorGraphByDescendingDegree(const TGraph& graph) {
    struct VertexDegree { VertexId vertex; std::size_t degree; };
    ArraySequence<VertexDegree> degrees;
    degrees.reserve(graph.vertexCount());
    for (const auto id : graph.vertexIds())
        degrees.push_back({id, graph.neighbors(id)->get().size()});
    heapSort(degrees, [](const VertexDegree& first, const VertexDegree& second) {
        return first.degree != second.degree ? first.degree > second.degree : first.vertex < second.vertex;
    });
    ArraySequence<VertexId> order;
    order.reserve(graph.vertexCount());
    for (const auto& entry : degrees) {
        order.push_back(entry.vertex);
    }
    return colorGraphGreedily(graph, std::move(order));
}

template <ColorableGraph TGraph>
struct ColoringAlgorithmDescriptor {
    ColoringAlgorithm id;
    std::string_view displayName;
    std::function<Result<ColoringResult>(const TGraph&)> colorGraph;
};

// Каждая запись связывает идентификатор, отображаемое имя и полную реализацию алгоритма
template <ColorableGraph TGraph>
inline const ColoringAlgorithmDescriptor<TGraph> coloringAlgorithmCatalog[] = {
#define DOR3_COLORING(id, displayName, implementation) \
    {ColoringAlgorithm::id, displayName, implementation<TGraph>},
#include "coloring_catalog.def"
#undef DOR3_COLORING
};

template <ColorableGraph TGraph>
Result<ColoringResult> colorGraph(const TGraph& graph, ColoringAlgorithm algorithm) {
    for (const auto& entry : coloringAlgorithmCatalog<TGraph>) {
        if (entry.id == algorithm) {
            return entry.colorGraph(graph);
        }
    }
    return failure(GraphErrorCode::InvalidParameters, "Unknown coloring algorithm.");
}

inline std::string_view algorithmName(ColoringAlgorithm algorithm) {
    for (const auto& entry : coloringAlgorithmCatalog<Graph>) {
        if (entry.id == algorithm) {
            return entry.displayName;
        }
    }
    return "Unknown";
}

template <ColorableGraph TGraph>
Result<void> validateColoring(const TGraph& graph, const ColoringResult& result) {
    const auto count = graph.vertexCount();
    if (result.vertexIds.size() != count || result.colors.size() != count ||
        result.processingOrder.size() != count || result.colorCount > count)
        return failure(GraphErrorCode::InvalidColoring, "Result size differs from graph size.");
    const auto ids = graph.vertexIds();
    ArraySequence<bool> seenVertices(count, false), usedColors(count, false);
    for (std::size_t i = 0; i < count; ++i) {
        if (result.vertexIds[i] != ids[i]) {
            return failure(GraphErrorCode::InvalidColoring, "Result vertex IDs differ from graph IDs.");
        }
    }
    for (std::size_t i = 0; i < count; ++i) {
        auto position = coloringVertexPosition(ids, result.processingOrder[i]);
        const auto color = result.colors[i];
        if (!position || seenVertices[*position] || color >= result.colorCount) {
            return failure(GraphErrorCode::InvalidColoring, "Invalid processing order or color.");
        }
        seenVertices[*position] = true;
        usedColors[color] = true;
        const auto& neighbors = graph.neighbors(ids[i])->get();
        for (const auto& neighbor : neighbors) {
            auto target = coloringVertexPosition(ids, neighbor.neighbor);
            if (!target || color == result.colors[*target]) {
                return failure(GraphErrorCode::InvalidColoring, "Adjacent vertices have the same color.");
            }
        }
    }
    for (std::size_t i = 0; i < result.colorCount; ++i)
        if (!usedColors[i]) {
            return failure(GraphErrorCode::InvalidColoring, "Unused color in color count.");
        }
    return {};
}
