#include <application.hpp>
#include <application_limits.hpp>
#include <storage.hpp>

Result<VertexId> GraphApplication::addVertex(std::string label) {
    if (label.find_first_of("\r\n") != std::string::npos) {
        return failure(GraphErrorCode::InvalidParameters, "Vertex labels must occupy one line.");
    }
    auto sizeValidation = validateGraphLimits(graph_.vertexCount() + 1, graph_.edgeCount());
    if (!sizeValidation) {
        return std::unexpected(sizeValidation.error());
    }
    auto id = graph_.addVertex({std::move(label)});
    if (id) handleGraphChange();
    return id;
}

Result<void> GraphApplication::setVertexData(VertexId id, std::string label) {
    if (label.find_first_of("\r\n") != std::string::npos) {
        return failure(GraphErrorCode::InvalidParameters, "Vertex labels must occupy one line.");
    }
    auto result = graph_.setVertexData(id, {std::move(label)});
    if (result) handleGraphChange();
    return result;
}

Result<void> GraphApplication::addEdge(VertexId first, VertexId second) {
    auto sizeValidation = validateGraphLimits(graph_.vertexCount(), graph_.edgeCount() + 1);
    if (!sizeValidation) {
        return sizeValidation;
    }
    auto result = graph_.addEdge(first, second);
    if (result) {
        handleGraphChange();
    }
    if (!result) return std::unexpected(result.error());
    return {};
}

Result<void> GraphApplication::removeEdge(VertexId first, VertexId second) {
    auto result = graph_.removeEdge(first, second);
    if (result) {
        handleGraphChange();
    }
    return result;
}

Result<void> GraphApplication::removeVertex(VertexId id) {
    auto result = graph_.removeVertex(id);
    if (result) handleGraphChange();
    return result;
}

Result<void> GraphApplication::generate(const GenerationParameters& parameters) {
    auto result = generateGraph(parameters);
    if (!result) {
        return std::unexpected(result.error());
    }
    graph_ = std::move(*result);
    handleGraphChange();
    return {};
}

Result<void> GraphApplication::open(const std::filesystem::path& path) {
    auto result = loadGraph(path);
    if (!result) {
        return std::unexpected(result.error());
    }
    graph_ = std::move(*result);
    coloring_.reset();
    modified_ = false;
    return {};
}

Result<void> GraphApplication::save(const std::filesystem::path& path) {
    auto result = saveGraph(graph_, path);
    if (result) {
        modified_ = false;
    }
    return result;
}

Result<void> GraphApplication::computeColoring(ColoringAlgorithm algorithm) {
    auto result = colorGraph(graph_, algorithm);
    if (!result) {
        return std::unexpected(result.error());
    }
    auto checked = validateColoring(graph_, *result);
    if (!checked) {
        return checked;
    }
    coloring_ = std::move(*result);
    return {};
}

Result<void> GraphApplication::compareAlgorithms(std::size_t repetitionCount) {
    auto result = ::compareAlgorithms(graph_, repetitionCount);
    if (!result) {
        return std::unexpected(result.error());
    }
    benchmarkResults_ = std::move(*result);
    return {};
}

Result<void> GraphApplication::compareSizeSeries(const GenerationParameters& parameters,
                                                std::size_t seriesPointCount,
                                                std::size_t repetitionCount) {
    auto result = ::compareSizeSeries(parameters, seriesPointCount, repetitionCount);
    if (!result) {
        return std::unexpected(result.error());
    }
    benchmarkResults_ = std::move(*result);
    return {};
}
