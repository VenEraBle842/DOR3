#pragma once
#include "graph.hpp"
#include <cstdint>
#include <string_view>

enum class Topology {
#define DOR3_TOPOLOGY(id, displayName, minimumVertexCount, usesRandomParameters, implementation) id,
#include "topology_catalog.def"
#undef DOR3_TOPOLOGY
    Count
};

struct GenerationParameters {
    std::size_t vertexCount = 20;
    Topology topology = Topology::Random;
    double edgeProbability = 0.15;
    std::uint32_t seed = 100;
};

struct TopologyDescriptor {
    Topology id;
    std::string_view displayName;
    std::size_t minimumVertexCount;
    bool usesRandomParameters;
    std::function<Result<void>(Graph&, const GenerationParameters&)> buildEdges;
};

// Каталог является общим для процессов генерации, проверки и графического интерфейса
extern const TopologyDescriptor topologyCatalog[static_cast<std::size_t>(Topology::Count)];
Result<std::reference_wrapper<const TopologyDescriptor>> findTopologyDescriptor(Topology topology);

Result<Graph> generateGraph(const GenerationParameters& parameters);
Result<void> validateGenerationParameters(const GenerationParameters& parameters);
std::string_view topologyName(Topology topology);
