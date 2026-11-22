#pragma once
#include "coloring.hpp"
#include "generator.hpp"
#include <filesystem>

namespace BenchmarkDefaults {
inline constexpr std::size_t repetitionCount = 7;
inline constexpr std::size_t sizeCount = 4;
}

struct BenchmarkRow {
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

Result<DynamicArray<BenchmarkRow>> compareAlgorithms(
    const Graph& graph, std::size_t repetitionCount = BenchmarkDefaults::repetitionCount);
Result<DynamicArray<BenchmarkRow>> compareSizeSeries(
    const GenerationParameters& parameters,
    std::size_t sizeCount = BenchmarkDefaults::sizeCount,
    std::size_t repetitionCount = BenchmarkDefaults::repetitionCount);
Result<void> exportBenchmark(const DynamicArray<BenchmarkRow>& rows,
                             const std::filesystem::path& path);
