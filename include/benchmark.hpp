#pragma once
#include "coloring.hpp"
#include "generator.hpp"
#include <filesystem>

struct BenchmarkRow {
    std::size_t vertices = 0;
    std::size_t edges = 0;
    ColoringAlgorithm algorithm = ColoringAlgorithm::Sequential;
    double medianMicroseconds = 0;
    std::size_t colors = 0;
    std::size_t repetitions = 0;
    bool verified = false;
    std::string topology = "Current graph";
    std::uint32_t seed = 0;
    double probability = 0;
};

Result<DynamicArray<BenchmarkRow>> compareAlgorithms(const Graph& graph,
                                                     std::size_t repetitions = 7);
Result<DynamicArray<BenchmarkRow>> compareSizes(const GenerationParameters& parameters,
                                                std::size_t points = 4,
                                                std::size_t repetitions = 7);
Result<void> exportBenchmark(const DynamicArray<BenchmarkRow>& rows,
                             const std::filesystem::path& path);
std::string_view topologyName(Topology topology);
