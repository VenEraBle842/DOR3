#include <charconv>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <application_limits.hpp>
#include <locale>
#include <sstream>
#include <storage.hpp>
#include <string_view>
#include <system_error>

namespace {
constexpr std::string_view graphFileHeader = "DOR3_GRAPH 2";
constexpr int maxTemporaryDirectoryAttempts = 16;
}

struct TemporaryDirectory {
    std::filesystem::path path;

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

static Result<std::filesystem::path> createTemporaryDirectory(const std::filesystem::path& target) {
    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    for (int attempt = 0; attempt < maxTemporaryDirectoryAttempts; ++attempt) {
        const auto path = target.parent_path() / (".dor3-write-" + std::to_string(timestamp) + "-" +
                                                  std::to_string(attempt));
        std::error_code error;
        if (std::filesystem::create_directory(path, error)) {
            return path;
        }
        if (error) {
            return failure(GraphErrorCode::FileWrite, target.string() + ": " + error.message());
        }
    }
    return failure(GraphErrorCode::FileWrite, "Cannot create a temporary file for " + target.string());
}

static bool readUnsigned(std::istream& input, std::size_t& value) {
    std::string token;
    if (!(input >> token)) {
        return false;
    }
    const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == token.data() + token.size();
}

Result<Graph> readGraph(std::istream& input) {
    std::string line;
    std::getline(input, line);
    if (line.ends_with('\r')) {
        line.pop_back();
    }
    const bool legacy = line == "DOR3_GRAPH 1";
    if (!input || (!legacy && line != graphFileHeader)) {
        return failure(GraphErrorCode::FileRead, "Expected " + std::string(graphFileHeader) + " header.");
    }
    std::size_t vertices = 0, edges = 0;
    if (!std::getline(input, line)) {
        return failure(GraphErrorCode::FileRead, "Missing counts.");
    }
    std::istringstream counts(line);
    counts.imbue(std::locale::classic());
    GraphIdCounter vertexCounter, edgeCounter;
    std::size_t vertexExhausted = 0, edgeExhausted = 0;
    if (!readUnsigned(counts, vertices) || !readUnsigned(counts, edges) ||
        (!legacy && (!readUnsigned(counts, vertexCounter.next) || !readUnsigned(counts, vertexExhausted) ||
                     !readUnsigned(counts, edgeCounter.next) || !readUnsigned(counts, edgeExhausted) ||
                     vertexExhausted > 1 || edgeExhausted > 1)) || !(counts >> std::ws).eof()) {
        return failure(GraphErrorCode::FileRead, "Invalid counts or identifier high-water marks.");
    }
    auto sizeValidation = validateGraphLimits(vertices, edges);
    if (!sizeValidation) {
        return failure(GraphErrorCode::FileRead, sizeValidation.error().detail);
    }
    Graph graph;
    for (std::size_t i = 0; i < vertices; ++i) {
        std::string label;
        if (!std::getline(input, line)) {
            return failure(GraphErrorCode::FileRead, "Missing vertex at index " + std::to_string(i));
        }
        std::istringstream record(line);
        VertexId id = i;
        if (!legacy && !readUnsigned(record, id)) {
            return failure(GraphErrorCode::FileRead, "Invalid vertex ID.");
        }
        record >> std::ws;
        if (record.peek() != '"' || !(record >> std::quoted(label)) || !(record >> std::ws).eof() ||
            label.find_first_of("\r\n") != std::string::npos) {
            return failure(GraphErrorCode::FileRead,
                           "Invalid quoted label at vertex " + std::to_string(i));
        }
        auto added = graph.addVertexWithId(id, {std::move(label)});
        if (!added) {
            return failure(GraphErrorCode::FileRead, formatError(added.error()));
        }
    }
    for (std::size_t i = 0; i < edges; ++i) {
        VertexId first = 0, second = 0;
        if (!std::getline(input, line)) {
            return failure(GraphErrorCode::FileRead, "Missing edge " + std::to_string(i));
        }
        std::istringstream record(line);
        record.imbue(std::locale::classic());
        EdgeId id = i;
        if ((!legacy && !readUnsigned(record, id)) || !readUnsigned(record, first) || !readUnsigned(record, second) ||
            !(record >> std::ws).eof()) {
            return failure(GraphErrorCode::FileRead, "Invalid edge " + std::to_string(i));
        }
        auto added = graph.addEdgeWithId(id, first, second);
        if (!added) {
            return failure(GraphErrorCode::FileRead,
                           "Edge " + std::to_string(i) + ": " + formatError(added.error()));
        }
    }
    if (!legacy) {
        vertexCounter.exhausted = vertexExhausted != 0;
        edgeCounter.exhausted = edgeExhausted != 0;
        auto advanced = graph.advanceIdCounters(vertexCounter, edgeCounter);
        if (!advanced) {
            return failure(GraphErrorCode::FileRead, formatError(advanced.error()));
        }
    }
    input >> std::ws;
    if (!input.eof() || input.bad()) {
        return failure(GraphErrorCode::FileRead, "Unexpected trailing data or read failure.");
    }
    return graph;
}

Result<void> writeGraph(const Graph& graph, std::ostream& output) {
    auto sizeValidation = validateGraphLimits(graph.vertexCount(), graph.edgeCount());
    if (!sizeValidation) {
        return sizeValidation;
    }
    output.imbue(std::locale::classic());
    const auto vertexCounter = graph.vertexIdCounter();
    const auto edgeCounter = graph.edgeIdCounter();
    output << std::dec << std::noboolalpha << std::noshowpos << std::noshowbase;
    output << graphFileHeader << '\n' << graph.vertexCount() << ' ' << graph.edgeCount() << ' '
           << vertexCounter.next << ' ' << vertexCounter.exhausted << ' '
           << edgeCounter.next << ' ' << edgeCounter.exhausted << '\n';
    for (const auto id : graph.vertexIds()) {
        const auto& label = graph.vertex(id)->get().data.label;
        if (label.find_first_of("\r\n") != std::string::npos) {
            return failure(GraphErrorCode::InvalidParameters, "Vertex labels must occupy one line.");
        }
        output << id << ' ' << std::quoted(label) << '\n';
    }
    for (const auto id : graph.edgeIds()) {
        const auto& edge = graph.edge(id)->get();
        output << id << ' ' << edge.first << ' ' << edge.second << '\n';
    }
    if (!output) {
        return failure(GraphErrorCode::FileWrite);
    }
    return {};
}

Result<Graph> loadGraph(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        return failure(GraphErrorCode::FileRead, path.string());
    }
    auto graph = readGraph(input);
    if (!graph) {
        graph.error().detail = path.string() + ": " + graph.error().detail;
    }
    return graph;
}

Result<void> writeFile(const std::filesystem::path& path,
                       const std::function<Result<void>(std::ostream&)>& writer) {
    auto directory = createTemporaryDirectory(path);
    if (!directory) {
        return std::unexpected(directory.error());
    }
    TemporaryDirectory cleanup{std::move(*directory)};
    const auto temporary = cleanup.path / "content";
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) {
        return failure(GraphErrorCode::FileWrite, path.string());
    }
    auto result = writer(output);
    output.flush();
    const bool flushed = static_cast<bool>(output);
    output.close();
    if (!result || !flushed || output.fail()) {
        if (!result) {
            return result;
        }
        return failure(GraphErrorCode::FileWrite, path.string());
    }
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        return failure(GraphErrorCode::FileWrite, path.string() + ": " + error.message());
    }
    return {};
}

Result<void> saveGraph(const Graph& graph, const std::filesystem::path& path) {
    auto sizeValidation = validateGraphLimits(graph.vertexCount(), graph.edgeCount());
    if (!sizeValidation) {
        return sizeValidation;
    }
    return writeFile(path, [&](std::ostream& output) {
        return writeGraph(graph, output);
    });
}
