#pragma once
#include "coloring.hpp"
#include "generator.hpp"
#include <filesystem>

namespace BenchmarkDefaults {
inline constexpr std::size_t repetitionCount = 7;
inline constexpr std::size_t seriesPointCount = 4;
}

struct BenchmarkResult {
    std::size_t vertexCount = 0;
    std::size_t edgeCount = 0;
    ColoringAlgorithm algorithm = ColoringAlgorithm::Sequential;
    double medianMicroseconds = 0;
    std::size_t colorCount = 0;
    std::size_t repetitionCount = 0;
    std::string topology = "Current graph";
    std::uint32_t seed = 0;
    double edgeProbability = 0;
};

Result<ArraySequence<BenchmarkResult>> compareAlgorithms(
    const Graph& graph, std::size_t repetitionCount = BenchmarkDefaults::repetitionCount);
Result<ArraySequence<BenchmarkResult>> compareSizeSeries(
    const GenerationParameters& parameters,
    std::size_t seriesPointCount = BenchmarkDefaults::seriesPointCount,
    std::size_t repetitionCount = BenchmarkDefaults::repetitionCount);
Result<void> exportBenchmark(const ArraySequence<BenchmarkResult>& rows,
                             const std::filesystem::path& path);
