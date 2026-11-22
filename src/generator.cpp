#include <cmath>
#include <generator.hpp>
#include <random>

Result<Graph> generateGraph(const GenerationParameters& parameters) {
    if (parameters.vertexCount > 2000 || !std::isfinite(parameters.probability) ||
        parameters.probability < 0 || parameters.probability > 1) {
        return failure(ErrorCode::InvalidParameters,
                       "Generation supports 0..2000 vertices and probability 0..1.");
    }
    if (parameters.topology < Topology::Path || parameters.topology > Topology::Random) {
        return failure(ErrorCode::InvalidParameters, "Unknown topology.");
    }
    if (parameters.topology == Topology::Cycle && parameters.vertexCount < 3) {
        return failure(ErrorCode::InvalidParameters, "A simple cycle needs at least 3 vertices.");
    }
    Graph graph;
    for (std::size_t i = 0; i < parameters.vertexCount; ++i) {
        graph.addVertex({std::to_string(i)});
    }
    auto connect = [&](std::size_t first, std::size_t second) {
        return graph.addEdge(first, second);
    };
    if (parameters.topology == Topology::Path || parameters.topology == Topology::Cycle) {
        for (std::size_t i = 1; i < parameters.vertexCount; ++i) {
            auto added = connect(i - 1, i);
            if (!added) {
                return std::unexpected(added.error());
            }
        }
        if (parameters.topology == Topology::Cycle) {
            auto added = connect(parameters.vertexCount - 1, 0);
            if (!added) {
                return std::unexpected(added.error());
            }
        }
    } else if (parameters.topology == Topology::Star) {
        for (std::size_t i = 1; i < parameters.vertexCount; ++i) {
            auto added = connect(0, i);
            if (!added) {
                return std::unexpected(added.error());
            }
        }
    } else {
        std::mt19937 random(parameters.seed);
        // Прямое преобразование результата генератора исключает различия реализаций распределений
        const auto threshold = parameters.probability * 4294967296.0;
        for (std::size_t i = 0; i < parameters.vertexCount; ++i) {
            for (std::size_t j = i + 1; j < parameters.vertexCount; ++j) {
                if (parameters.topology == Topology::Complete ||
                    static_cast<double>(random()) < threshold) {
                    auto added = connect(i, j);
                    if (!added) {
                        return std::unexpected(added.error());
                    }
                }
            }
        }
    }
    return graph;
}
