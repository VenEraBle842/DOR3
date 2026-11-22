#include <cmath>
#include <generator.hpp>
#include <input_limits.hpp>
#include <random>

std::string_view topologyName(Topology topology) {
    switch (topology) {
    case Topology::Path:
        return "Path";
    case Topology::Cycle:
        return "Cycle";
    case Topology::Star:
        return "Star";
    case Topology::Complete:
        return "Complete";
    case Topology::Random:
        return "Random";
    }
    return "Unknown";
}

Result<void> validateGenerationParameters(const GenerationParameters& parameters) {
    if (parameters.vertexCount > InputLimits::maxGeneratedVertices ||
        !std::isfinite(parameters.probability) ||
        parameters.probability < 0 || parameters.probability > 1) {
        return failure(ErrorCode::InvalidParameters,
                       "Generation supports 0.." +
                           std::to_string(InputLimits::maxGeneratedVertices) +
                           " vertices and probability 0..1.");
    }
    if (parameters.topology < Topology::Path || parameters.topology > Topology::Random) {
        return failure(ErrorCode::InvalidParameters, "Unknown topology.");
    }
    if (parameters.topology == Topology::Cycle && parameters.vertexCount < 3) {
        return failure(ErrorCode::InvalidParameters, "A simple cycle needs at least 3 vertices.");
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
    if (parameters.topology == Topology::Path || parameters.topology == Topology::Cycle) {
        for (std::size_t i = 1; i < parameters.vertexCount; ++i) {
            auto added = graph.addEdge(i - 1, i);
            if (!added) {
                return std::unexpected(added.error());
            }
        }
        if (parameters.topology == Topology::Cycle) {
            auto added = graph.addEdge(parameters.vertexCount - 1, 0);
            if (!added) {
                return std::unexpected(added.error());
            }
        }
    } else if (parameters.topology == Topology::Star) {
        for (std::size_t i = 1; i < parameters.vertexCount; ++i) {
            auto added = graph.addEdge(0, i);
            if (!added) {
                return std::unexpected(added.error());
            }
        }
    } else {
        std::mt19937 random(parameters.seed);
        // Прямое преобразование результата генератора исключает различия реализаций распределений
        constexpr auto randomRange = static_cast<double>(std::mt19937::max()) -
                                     static_cast<double>(std::mt19937::min()) + 1.0;
        const auto threshold = parameters.probability * randomRange;
        for (std::size_t i = 0; i < parameters.vertexCount; ++i) {
            for (std::size_t j = i + 1; j < parameters.vertexCount; ++j) {
                if (parameters.topology == Topology::Complete ||
                    static_cast<double>(random()) < threshold) {
                    auto added = graph.addEdge(i, j);
                    if (!added) {
                        return std::unexpected(added.error());
                    }
                }
            }
        }
    }
    return graph;
}
