#include <cmath>
#include <generator.hpp>
#include <application_limits.hpp>
#include <random>
#include <iterator>

Result<void> buildPathEdges(Graph& graph, const GenerationParameters&) {
    const auto ids = graph.vertexIds();
    for (std::size_t position = 1; position < ids.size(); ++position) {
        auto added = graph.addEdge(ids[position - 1], ids[position]);
        if (!added) {
            return std::unexpected(added.error());
        }
    }
    return {};
}

Result<void> buildCycleEdges(Graph& graph, const GenerationParameters& parameters) {
    if (parameters.vertexCount < 3) {
        return failure(GraphErrorCode::InvalidParameters, "A cycle needs at least three vertices.");
    }
    auto path = buildPathEdges(graph, parameters);
    if (!path) {
        return path;
    }
    const auto ids = graph.vertexIds();
    auto added = graph.addEdge(ids[ids.size() - 1], ids[0]);
    if (!added) {
        return std::unexpected(added.error());
    }
    return {};
}

Result<void> buildStarEdges(Graph& graph, const GenerationParameters&) {
    const auto ids = graph.vertexIds();
    for (std::size_t position = 1; position < ids.size(); ++position) {
        auto added = graph.addEdge(ids[0], ids[position]);
        if (!added) {
            return std::unexpected(added.error());
        }
    }
    return {};
}

Result<void> buildCompleteEdges(Graph& graph, const GenerationParameters&) {
    const auto ids = graph.vertexIds();
    for (std::size_t first = 0; first < ids.size(); ++first) {
        for (std::size_t second = first + 1; second < ids.size(); ++second) {
            auto added = graph.addEdge(ids[first], ids[second]);
            if (!added) {
                return std::unexpected(added.error());
            }
        }
    }
    return {};
}

Result<void> buildRandomEdges(Graph& graph, const GenerationParameters& parameters) {
    const auto ids = graph.vertexIds();
    std::mt19937 random(parameters.seed);
    // Прямое преобразование обеспечивает воспроизводимость в различных реализациях
    constexpr auto randomRange = static_cast<double>(std::mt19937::max()) -
                                 static_cast<double>(std::mt19937::min()) + 1.0;
    const auto threshold = parameters.edgeProbability * randomRange;
    for (std::size_t first = 0; first < ids.size(); ++first) {
        for (std::size_t second = first + 1; second < ids.size(); ++second) {
            if (static_cast<double>(random()) < threshold) {
                auto added = graph.addEdge(ids[first], ids[second]);
                if (!added) {
                    return std::unexpected(added.error());
                }
            }
        }
    }
    return {};
}

template <class TBuilder>
Result<void> buildTopologyEdges(Graph& graph, const GenerationParameters& parameters, TBuilder builder) {
    if (graph.vertexCount() != parameters.vertexCount) {
        return failure(GraphErrorCode::InvalidParameters, "Topology vertex count differs from graph size.");
    }
    return builder(graph, parameters);
}

const TopologyDescriptor topologyCatalog[] = {
#define DOR3_TOPOLOGY(id, displayName, minimumVertexCount, usesRandomParameters, implementation) \
    {Topology::id, displayName, minimumVertexCount, usesRandomParameters, \
     [](Graph& graph, const GenerationParameters& parameters) { \
         return buildTopologyEdges(graph, parameters, implementation); \
     }},
#include "topology_catalog.def"
#undef DOR3_TOPOLOGY
};

Result<std::reference_wrapper<const TopologyDescriptor>> findTopologyDescriptor(Topology topology) {
    for (std::size_t i = 0; i < std::size(topologyCatalog); ++i) {
        if (topologyCatalog[i].id == topology) {
            return std::cref(topologyCatalog[i]);
        }
    }
    return failure(GraphErrorCode::InvalidParameters, "Unknown topology.");
}

std::string_view topologyName(Topology topology) {
    const auto entry = findTopologyDescriptor(topology);
    return entry ? entry->get().displayName : "Unknown";
}

Result<void> validateGenerationParameters(const GenerationParameters& parameters) {
    const auto entry = findTopologyDescriptor(parameters.topology);
    if (!entry) {
        return std::unexpected(entry.error());
    }
    const auto& topology = entry->get();
    if (parameters.vertexCount < topology.minimumVertexCount ||
        parameters.vertexCount > ApplicationLimits::maxGeneratedVertices) {
        return failure(GraphErrorCode::InvalidParameters,
                       std::string(topology.displayName) + " supports " +
                           std::to_string(topology.minimumVertexCount) + ".." +
                           std::to_string(ApplicationLimits::maxGeneratedVertices) + " vertices.");
    }
    if (topology.usesRandomParameters &&
        (!std::isfinite(parameters.edgeProbability) ||
         parameters.edgeProbability < 0 || parameters.edgeProbability > 1)) {
        return failure(GraphErrorCode::InvalidParameters, "Probability must be finite and within 0..1.");
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
        auto added = graph.addVertex({std::to_string(i)});
        if (!added) {
            return std::unexpected(added.error());
        }
    }
    auto entry = findTopologyDescriptor(parameters.topology);
    if (!entry) {
        return std::unexpected(entry.error());
    }
    auto built = entry->get().buildEdges(graph, parameters);
    if (!built) {
        return std::unexpected(built.error());
    }
    return graph;
}
