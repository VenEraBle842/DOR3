#pragma once
#include "coloring.hpp"
#include "generator.hpp"
#include <filesystem>

namespace BenchmarkDefaults {
inline constexpr std::size_t repetitions = 7;
inline constexpr std::size_t points = 4;
}

struct BenchmarkRow {
    std::size_t vertices = 0;
    std::size_t edges = 0;
    ColoringAlgorithm algorithm = ColoringAlgorithm::Sequential;
    double medianMicroseconds = 0;
    std::size_t colors = 0;
    std::size_t repetitions = 0;
    std::string topology = "Current graph";
    std::uint32_t seed = 0;
    double probability = 0;
};

Result<DynamicArray<BenchmarkRow>> compareAlgorithms(
    const Graph& graph, std::size_t repetitions = BenchmarkDefaults::repetitions);
Result<DynamicArray<BenchmarkRow>> compareSizes(
    const GenerationParameters& parameters,
    std::size_t points = BenchmarkDefaults::points,
    std::size_t repetitions = BenchmarkDefaults::repetitions);
Result<void> exportBenchmark(const DynamicArray<BenchmarkRow>& rows,
                             const std::filesystem::path& path);
