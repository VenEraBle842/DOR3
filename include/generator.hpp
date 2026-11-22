#pragma once
#include "graph.hpp"
#include <cstdint>
#include <string_view>

enum class Topology { Path, Cycle, Star, Complete, Random };

struct GenerationParameters {
    std::size_t vertexCount = 20;
    Topology topology = Topology::Random;
    double probability = 0.15;
    std::uint32_t seed = 100;
};

Result<Graph> generateGraph(const GenerationParameters& parameters);
Result<void> validateGenerationParameters(const GenerationParameters& parameters);
std::string_view topologyName(Topology topology);
