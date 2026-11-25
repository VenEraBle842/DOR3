#pragma once
#include "graph.hpp"
#include <cstdint>
#include <string_view>

enum class Topology { Path, Cycle, Star, Complete, Random };

struct GenerationParameters {
    std::size_t vertexCount = 20;
    Topology topology = Topology::Random;
    double edgeProbability = 0.15;
    std::uint32_t seed = 100;
};

struct TopologyDescriptor {
    Topology id;
    std::string_view name;
    std::size_t minimumVertexCount;
    bool usesRandomParameters;
    std::function<Result<void>(Graph&, const GenerationParameters&)> buildEdges;
};

// Каталог является общим для процессов генерации, проверки и графического интерфейса
extern const DynamicArray<TopologyDescriptor> topologies;
Result<std::reference_wrapper<const TopologyDescriptor>> findTopology(Topology topology);

Result<Graph> generateGraph(const GenerationParameters& parameters);
Result<void> validateGenerationParameters(const GenerationParameters& parameters);
std::string_view topologyName(Topology topology);
