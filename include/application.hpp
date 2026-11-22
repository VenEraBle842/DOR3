#pragma once
#include "benchmark.hpp"
#include <optional>

class GraphApplication {
    Graph graph_;
    std::optional<ColoringResult> coloring_;
    DynamicArray<BenchmarkRow> measurements_;
    bool modified_ = false;

    void handleGraphChange() {
        coloring_.reset();
        modified_ = true;
    }

public:
    const Graph& graph() const noexcept {
        return graph_;
    }

    const std::optional<ColoringResult>& coloring() const noexcept {
        return coloring_;
    }

    const DynamicArray<BenchmarkRow>& measurements() const noexcept {
        return measurements_;
    }

    bool isModified() const noexcept {
        return modified_;
    }

    void clear() {
        graph_ = Graph{};
        handleGraphChange();
    }

    Result<VertexId> addVertex(std::string label);
    Result<void> addEdge(VertexId first, VertexId second);
    Result<void> removeEdge(VertexId first, VertexId second);
    Result<void> generate(const GenerationParameters& parameters);
    Result<void> open(const std::filesystem::path& path);
    Result<void> save(const std::filesystem::path& path);
    Result<void> computeColoring(ColoringAlgorithm algorithm);
    Result<void> compareAlgorithms(std::size_t repetitionCount);
    Result<void> compareSizeSeries(const GenerationParameters& parameters,
                                   std::size_t sizeCount,
                                   std::size_t repetitionCount);
};
