#pragma once
#include "benchmark.hpp"
#include <optional>

class GraphApplication {
    Graph graph_;
    std::optional<ColoringResult> coloring_;
    ArraySequence<BenchmarkResult> benchmarkResults_;
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

    const ArraySequence<BenchmarkResult>& benchmarkResults() const noexcept {
        return benchmarkResults_;
    }

    bool isModified() const noexcept {
        return modified_;
    }

    void clear() {
        graph_.clear();
        handleGraphChange();
    }

    Result<VertexId> addVertex(std::string label);
    Result<void> setVertexData(VertexId id, std::string label);
    Result<void> addEdge(VertexId first, VertexId second);
    Result<void> removeEdge(VertexId first, VertexId second);
    Result<void> removeVertex(VertexId id);
    Result<void> generate(const GenerationParameters& parameters);
    Result<void> open(const std::filesystem::path& path);
    Result<void> save(const std::filesystem::path& path);
    Result<void> computeColoring(ColoringAlgorithm algorithm);
    Result<void> compareAlgorithms(std::size_t repetitionCount);
    Result<void> compareSizeSeries(const GenerationParameters& parameters,
                                   std::size_t seriesPointCount,
                                   std::size_t repetitionCount);
};
