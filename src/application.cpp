#include <application.hpp>
#include <storage.hpp>

Result<VertexId> GraphApplication::addVertex(std::string label) {
    if (label.find_first_of("\r\n") != std::string::npos) {
        return failure(ErrorCode::InvalidParameters, "Vertex labels must occupy one line.");
    }
    auto id = graph_.addVertex({std::move(label)});
    changed();
    return id;
}

Result<void> GraphApplication::addEdge(VertexId first, VertexId second) {
    auto result = graph_.addEdge(first, second);
    if (result) {
        changed();
    }
    return result;
}

Result<void> GraphApplication::removeEdge(VertexId first, VertexId second) {
    auto result = graph_.removeEdge(first, second);
    if (result) {
        changed();
    }
    return result;
}

Result<void> GraphApplication::generate(const GenerationParameters& parameters) {
    auto result = generateGraph(parameters);
    if (!result) {
        return std::unexpected(result.error());
    }
    graph_ = std::move(*result);
    changed();
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

Result<void> GraphApplication::run(ColoringAlgorithm algorithm) {
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

Result<void> GraphApplication::compare(std::size_t repetitions) {
    auto result = compareAlgorithms(graph_, repetitions);
    if (!result) {
        return std::unexpected(result.error());
    }
    measurements_ = std::move(*result);
    return {};
}

Result<void> GraphApplication::compareSeries(const GenerationParameters& parameters,
                                             std::size_t points,
                                             std::size_t repetitions) {
    auto result = compareSizes(parameters, points, repetitions);
    if (!result) {
        return std::unexpected(result.error());
    }
    measurements_ = std::move(*result);
    return {};
}
