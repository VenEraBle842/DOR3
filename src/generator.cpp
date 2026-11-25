#include <cmath>
#include <generator.hpp>
#include <application_limits.hpp>
#include <random>

Result<void> buildPathEdges(Graph& graph, const GenerationParameters& parameters) {
    for (VertexId id = 1; id < parameters.vertexCount; ++id) {
        auto added = graph.addEdge(id - 1, id);
        if (!added) {
            return added;
        }
    }
    return {};
}

Result<void> buildCycleEdges(Graph& graph, const GenerationParameters& parameters) {
    auto path = buildPathEdges(graph, parameters);
    if (!path) {
        return path;
    }
    return graph.addEdge(parameters.vertexCount - 1, 0);
}

Result<void> buildStarEdges(Graph& graph, const GenerationParameters& parameters) {
    for (VertexId id = 1; id < parameters.vertexCount; ++id) {
        auto added = graph.addEdge(0, id);
        if (!added) {
            return added;
        }
    }
    return {};
}

Result<void> buildCompleteEdges(Graph& graph, const GenerationParameters& parameters) {
    for (VertexId first = 0; first < parameters.vertexCount; ++first) {
        for (VertexId second = first + 1; second < parameters.vertexCount; ++second) {
            auto added = graph.addEdge(first, second);
            if (!added) {
                return added;
            }
        }
    }
    return {};
}

Result<void> buildRandomEdges(Graph& graph, const GenerationParameters& parameters) {
    std::mt19937 random(parameters.seed);
    // Прямое преобразование обеспечивает воспроизводимость в различных реализациях
    constexpr auto randomRange = static_cast<double>(std::mt19937::max()) -
                                 static_cast<double>(std::mt19937::min()) + 1.0;
    const auto threshold = parameters.edgeProbability * randomRange;
    for (VertexId first = 0; first < parameters.vertexCount; ++first) {
        for (VertexId second = first + 1; second < parameters.vertexCount; ++second) {
            if (static_cast<double>(random()) < threshold) {
                auto added = graph.addEdge(first, second);
                if (!added) {
                    return added;
                }
            }
        }
    }
    return {};
}

const DynamicArray<TopologyDescriptor> topologies{
    {Topology::Path, "Path", 0, false, buildPathEdges},
    {Topology::Cycle, "Cycle", 3, false, buildCycleEdges},
    {Topology::Star, "Star", 0, false, buildStarEdges},
    {Topology::Complete, "Complete", 0, false, buildCompleteEdges},
    {Topology::Random, "Random", 0, true, buildRandomEdges}};

Result<std::reference_wrapper<const TopologyDescriptor>> findTopology(Topology topology) {
    for (std::size_t i = 0; i < topologies.size(); ++i) {
        if (topologies[i].id == topology) {
            return std::cref(topologies[i]);
        }
    }
    return failure(ErrorCode::InvalidParameters, "Unknown topology.");
}

std::string_view topologyName(Topology topology) {
    const auto entry = findTopology(topology);
    return entry ? entry->get().name : "Unknown";
}

Result<void> validateGenerationParameters(const GenerationParameters& parameters) {
    const auto entry = findTopology(parameters.topology);
    if (!entry) {
        return std::unexpected(entry.error());
    }
    const auto& topology = entry->get();
    if (parameters.vertexCount < topology.minimumVertexCount ||
        parameters.vertexCount > ApplicationLimits::maxGeneratedVertices) {
        return failure(ErrorCode::InvalidParameters,
                       std::string(topology.name) + " supports " +
                           std::to_string(topology.minimumVertexCount) + ".." +
                           std::to_string(ApplicationLimits::maxGeneratedVertices) + " vertices.");
    }
    if (topology.usesRandomParameters &&
        (!std::isfinite(parameters.edgeProbability) ||
         parameters.edgeProbability < 0 || parameters.edgeProbability > 1)) {
        return failure(ErrorCode::InvalidParameters, "Probability must be finite and within 0..1.");
    }
    return {};
}

Result<Graph> generateGraph(const GenerationParameters& parameters) {
    auto valid = validateGenerationParameters(parameters);
    if (!valid) {
        return std::unexpected(valid.error());
    }
    Graph graph;
    for (std::size_t i = 0; i < parameters.vertexCount; ++i) {
        graph.addVertex({std::to_string(i)});
    }
    auto entry = findTopology(parameters.topology);
    if (!entry) {
        return std::unexpected(entry.error());
    }
    auto built = entry->get().buildEdges(graph, parameters);
    if (!built) {
        return std::unexpected(built.error());
    }
    return graph;
}
