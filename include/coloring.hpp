#pragma once

#include "graph.hpp"
#include <common/heap_sort.hpp>
#include <limits>
#include <string_view>

enum class ColoringAlgorithm { Sequential, LargestDegreeFirst };

inline constexpr ColoringAlgorithm supportedColoringAlgorithms[] = {
    ColoringAlgorithm::Sequential, ColoringAlgorithm::LargestDegreeFirst};

inline std::string_view algorithmName(ColoringAlgorithm algorithm) {
    switch (algorithm) {
    case ColoringAlgorithm::Sequential:
        return "Sequential greedy";
    case ColoringAlgorithm::LargestDegreeFirst:
        return "Largest degree first";
    }
    return "Unknown";
}

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
Result<ColoringResult> colorGraph(const TGraph& graph, ColoringAlgorithm algorithm) {
    if (algorithm != ColoringAlgorithm::Sequential &&
        algorithm != ColoringAlgorithm::LargestDegreeFirst) {
        return failure(ErrorCode::InvalidParameters, "Unknown coloring algorithm.");
    }
    constexpr auto uncolored = std::numeric_limits<std::size_t>::max();
    ColoringResult result;
    const auto count = graph.vertexCount();
    result.colors = DynamicArray<std::size_t>(count);
    result.processingOrder = DynamicArray<VertexId>(count);
    DynamicArray<std::size_t> marks(count);
    for (std::size_t i = 0; i < count; ++i) {
        result.colors.push_back(uncolored);
        result.processingOrder.push_back(i);
        marks.push_back(uncolored);
    }
    if (algorithm == ColoringAlgorithm::LargestDegreeFirst) {
        struct VertexDegree {
            VertexId vertex;
            std::size_t degree;
        };

        DynamicArray<VertexDegree> degrees(count);
        for (std::size_t i = 0; i < count; ++i) {
            degrees.push_back({i, graph.neighbors(i)->get().size()});
        }
        heapSort(degrees, [](const VertexDegree& first, const VertexDegree& second) {
            return first.degree != second.degree ? first.degree > second.degree
                                                 : first.vertex < second.vertex;
        });
        for (std::size_t i = 0; i < count; ++i) {
            result.processingOrder[i] = degrees[i].vertex;
        }
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
