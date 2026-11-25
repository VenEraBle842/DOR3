#pragma once

#include "graph.hpp"
#include <common/heap_sort.hpp>
#include <limits>
#include <functional>
#include <string_view>

enum class ColoringAlgorithm { Sequential, LargestDegreeFirst };

struct ColoringResult {
    DynamicArray<std::size_t> colors;
    DynamicArray<VertexId> processingOrder;
    std::size_t colorCount = 0;
};

template <class TGraph>
concept ColorableGraph = requires(const TGraph& graph, VertexId id) {
    { graph.vertexCount() } -> std::convertible_to<std::size_t>;
    {
        graph.neighbors(id)
    } -> std::same_as<Result<std::reference_wrapper<const DynamicArray<VertexId>>>>;
};

template <ColorableGraph TGraph>
Result<ColoringResult> greedyColorGraph(const TGraph& graph, DynamicArray<VertexId> order) {
    const auto count = graph.vertexCount();
    if (order.size() != count) {
        return failure(ErrorCode::InvalidParameters, "Order must contain every vertex once.");
    }
    DynamicArray<bool> seen(count);
    for (std::size_t i = 0; i < count; ++i) {
        seen.push_back(false);
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (order[i] >= count || seen[order[i]]) {
            return failure(ErrorCode::InvalidParameters, "Order must contain every vertex once.");
        }
        seen[order[i]] = true;
    }
    constexpr auto uncolored = std::numeric_limits<std::size_t>::max();
    ColoringResult result;
    result.processingOrder = std::move(order);
    result.colors = DynamicArray<std::size_t>(count);
    DynamicArray<std::size_t> marks(count);
    for (std::size_t i = 0; i < count; ++i) {
        result.colors.push_back(uncolored);
        marks.push_back(uncolored);
    }
    for (std::size_t step = 0; step < count; ++step) {
        const auto vertex = result.processingOrder[step];
        const auto& neighbors = graph.neighbors(vertex)->get();
        for (std::size_t i = 0; i < neighbors.size(); ++i) {
            const auto color = result.colors[neighbors[i]];
            if (color != uncolored) {
                marks[color] = step;
            }
        }
        std::size_t color = 0;
        while (color < count && marks[color] == step) {
            ++color;
        }
        result.colors[vertex] = color;
        if (color + 1 > result.colorCount) {
            result.colorCount = color + 1;
        }
    }
    return result;
}

template <ColorableGraph TGraph>
Result<ColoringResult> colorSequential(const TGraph& graph) {
    DynamicArray<VertexId> order(graph.vertexCount());
    for (VertexId id = 0; id < graph.vertexCount(); ++id) {
        order.push_back(id);
    }
    return greedyColorGraph(graph, std::move(order));
}

template <ColorableGraph TGraph>
Result<ColoringResult> colorLargestDegreeFirst(const TGraph& graph) {
    struct VertexDegree {
        VertexId vertex;
        std::size_t degree;
    };
    DynamicArray<VertexDegree> degrees(graph.vertexCount());
    for (VertexId id = 0; id < graph.vertexCount(); ++id) {
        degrees.push_back({id, graph.neighbors(id)->get().size()});
    }
    heapSort(degrees, [](const VertexDegree& first, const VertexDegree& second) {
        return first.degree != second.degree ? first.degree > second.degree
                                             : first.vertex < second.vertex;
    });
    DynamicArray<VertexId> order(graph.vertexCount());
    for (std::size_t i = 0; i < degrees.size(); ++i) {
        order.push_back(degrees[i].vertex);
    }
    return greedyColorGraph(graph, std::move(order));
}

template <ColorableGraph TGraph>
struct ColoringAlgorithmDescriptor {
    ColoringAlgorithm id;
    std::string_view name;
    std::function<Result<ColoringResult>(const TGraph&)> run;
};

// Каждая запись связывает идентификатор, отображаемое имя и полную реализацию алгоритма
template <ColorableGraph TGraph>
inline const ColoringAlgorithmDescriptor<TGraph> coloringAlgorithms[] = {
    {ColoringAlgorithm::Sequential, "Sequential greedy", colorSequential<TGraph>},
    {ColoringAlgorithm::LargestDegreeFirst, "Largest degree first", colorLargestDegreeFirst<TGraph>}};

template <ColorableGraph TGraph>
Result<ColoringResult> colorGraph(const TGraph& graph, ColoringAlgorithm algorithm) {
    for (const auto& entry : coloringAlgorithms<TGraph>) {
        if (entry.id == algorithm) {
            return entry.run(graph);
        }
    }
    return failure(ErrorCode::InvalidParameters, "Unknown coloring algorithm.");
}

inline std::string_view algorithmName(ColoringAlgorithm algorithm) {
    for (const auto& entry : coloringAlgorithms<Graph>) {
        if (entry.id == algorithm) {
            return entry.name;
        }
    }
    return "Unknown";
}

template <ColorableGraph TGraph>
Result<void> validateColoring(const TGraph& graph, const ColoringResult& result) {
    const auto count = graph.vertexCount();
    if (result.colors.size() != count || result.processingOrder.size() != count ||
        result.colorCount > count) {
        return failure(ErrorCode::InvalidColoring, "Result size differs from graph size.");
    }
    DynamicArray<bool> seenVertices;
    DynamicArray<bool> usedColors;
    for (std::size_t i = 0; i < count; ++i) {
        seenVertices.push_back(false);
        usedColors.push_back(false);
    }
    for (std::size_t i = 0; i < count; ++i) {
        const auto vertex = result.processingOrder[i];
        const auto color = result.colors[i];
        if (vertex >= count || seenVertices[vertex] || color >= result.colorCount) {
            return failure(ErrorCode::InvalidColoring, "Invalid processing order or color.");
        }
        seenVertices[vertex] = true;
        usedColors[color] = true;
        const auto& neighbors = graph.neighbors(i)->get();
        for (std::size_t j = 0; j < neighbors.size(); ++j) {
            if (color == result.colors[neighbors[j]]) {
                return failure(ErrorCode::InvalidColoring,
                               "Adjacent vertices have the same color.");
            }
        }
    }
    for (std::size_t i = 0; i < result.colorCount; ++i) {
        if (!usedColors[i]) {
            return failure(ErrorCode::InvalidColoring, "Unused color in color count.");
        }
    }
    return {};
}
