#include <chrono>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <storage.hpp>
#include <system_error>

struct TemporaryDirectory {
    std::filesystem::path path;

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

static Result<std::filesystem::path> temporaryDirectory(const std::filesystem::path& target) {
    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    for (int attempt = 0; attempt < 16; ++attempt) {
        const auto path = target.parent_path() / (".dor3-write-" + std::to_string(timestamp) + "-" +
                                                  std::to_string(attempt));
        std::error_code error;
        if (std::filesystem::create_directory(path, error)) {
            return path;
        }
        if (error) {
            return failure(ErrorCode::FileWrite, target.string() + ": " + error.message());
        }
    }
    return failure(ErrorCode::FileWrite, "Cannot create a temporary file for " + target.string());
}

Result<Graph> readGraph(std::istream& input) {
    std::string line;
    if (!std::getline(input, line) || (line != "DOR3_GRAPH 1" && line != "DOR3_GRAPH 1\r")) {
        return failure(ErrorCode::FileRead, "Expected DOR3_GRAPH 1 header.");
    }
    std::size_t vertices = 0, edges = 0;
    if (!std::getline(input, line)) {
        return failure(ErrorCode::FileRead, "Missing counts.");
    }
    std::istringstream counts(line);
    counts.imbue(std::locale::classic());
    if (!(counts >> vertices >> edges) || !(counts >> std::ws).eof() || vertices > 100000 ||
        edges > 2000000) {
        return failure(ErrorCode::FileRead,
                       "Invalid counts (limits: 100000 vertices, 2000000 edges).");
    }
    Graph graph;
    for (std::size_t i = 0; i < vertices; ++i) {
        std::string label;
        if (!std::getline(input, line)) {
            return failure(ErrorCode::FileRead, "Missing vertex at index " + std::to_string(i));
        }
        std::istringstream record(line);
        record >> std::ws;
        if (record.peek() != '"' || !(record >> std::quoted(label)) || !(record >> std::ws).eof() ||
            label.find_first_of("\r\n") != std::string::npos) {
            return failure(ErrorCode::FileRead,
                           "Invalid quoted label at vertex " + std::to_string(i));
        }
        graph.addVertex({std::move(label)});
    }
    for (std::size_t i = 0; i < edges; ++i) {
        VertexId first = 0, second = 0;
        if (!std::getline(input, line)) {
            return failure(ErrorCode::FileRead, "Missing edge " + std::to_string(i));
        }
        std::istringstream record(line);
        record.imbue(std::locale::classic());
        if (!(record >> first >> second) || !(record >> std::ws).eof()) {
            return failure(ErrorCode::FileRead, "Invalid edge " + std::to_string(i));
        }
        auto added = graph.addEdge(first, second);
        if (!added) {
            return failure(ErrorCode::FileRead,
                           "Edge " + std::to_string(i) + ": " + formatError(added.error()));
        }
    }
    input >> std::ws;
    if (!input.eof() || input.bad()) {
        return failure(ErrorCode::FileRead, "Unexpected trailing data or read failure.");
    }
    return graph;
}

Result<void> writeGraph(const Graph& graph, std::ostream& output) {
    output.imbue(std::locale::classic());
    output << "DOR3_GRAPH 1\n" << graph.vertexCount() << ' ' << graph.edgeCount() << '\n';
    for (std::size_t i = 0; i < graph.vertexCount(); ++i) {
        const auto& label = graph.vertex(i)->get().label;
        if (label.find_first_of("\r\n") != std::string::npos) {
            return failure(ErrorCode::InvalidParameters, "Vertex labels must occupy one line.");
        }
        output << std::quoted(label) << '\n';
    }
    for (std::size_t i = 0; i < graph.vertexCount(); ++i) {
        const auto& neighbors = graph.neighbors(i)->get();
        for (std::size_t j = 0; j < neighbors.size(); ++j) {
            if (i < neighbors[j]) {
                output << i << ' ' << neighbors[j] << '\n';
            }
        }
    }
    if (!output) {
        return failure(ErrorCode::FileWrite);
    }
    return {};
}

Result<Graph> loadGraph(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        return failure(ErrorCode::FileRead, path.string());
    }
    auto graph = readGraph(input);
    if (!graph) {
        graph.error().detail = path.string() + ": " + graph.error().detail;
    }
    return graph;
}

Result<void> writeFile(const std::filesystem::path& path,
                       const std::function<Result<void>(std::ostream&)>& writer) {
    auto directory = temporaryDirectory(path);
    if (!directory) {
        return std::unexpected(directory.error());
    }
    TemporaryDirectory cleanup{std::move(*directory)};
    const auto temporary = cleanup.path / "content";
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) {
        return failure(ErrorCode::FileWrite, path.string());
    }
    auto result = writer(output);
    output.flush();
    const bool flushed = static_cast<bool>(output);
    output.close();
    if (!result || !flushed || output.fail()) {
        if (!result) {
            return result;
        }
        return failure(ErrorCode::FileWrite, path.string());
    }
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        return failure(ErrorCode::FileWrite, path.string() + ": " + error.message());
    }
    return {};
}

Result<void> saveGraph(const Graph& graph, const std::filesystem::path& path) {
    return writeFile(path, [&](std::ostream& output) {
        return writeGraph(graph, output);
    });
}
