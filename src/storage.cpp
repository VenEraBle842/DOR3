#include <charconv>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <input_limits.hpp>
#include <locale>
#include <sstream>
#include <storage.hpp>
#include <string_view>
#include <system_error>

namespace {
constexpr std::string_view graphFileHeader = "DOR3_GRAPH 1";
constexpr int maxTemporaryDirectoryAttempts = 16;
}

struct TemporaryDirectory {
    std::filesystem::path path;

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

static Result<std::filesystem::path> temporaryDirectory(const std::filesystem::path& target) {
    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    for (int attempt = 0; attempt < maxTemporaryDirectoryAttempts; ++attempt) {
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
    if (!input || line != graphFileHeader) {
        return failure(ErrorCode::FileRead, "Expected " + std::string(graphFileHeader) + " header.");
    }
    std::size_t vertices = 0, edges = 0;
    if (!std::getline(input, line)) {
        return failure(ErrorCode::FileRead, "Missing counts.");
    }
    std::istringstream counts(line);
    counts.imbue(std::locale::classic());
    if (!readUnsigned(counts, vertices) || !readUnsigned(counts, edges) ||
        !(counts >> std::ws).eof()) {
        return failure(ErrorCode::FileRead, "Expected two unsigned decimal counts.");
    }
    auto size = validateGraphSize(vertices, edges);
    if (!size) {
        return failure(ErrorCode::FileRead, size.error().detail);
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
        if (!readUnsigned(record, first) || !readUnsigned(record, second) ||
            !(record >> std::ws).eof()) {
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
    auto size = validateGraphSize(graph.vertexCount(), graph.edgeCount());
    if (!size) {
        return size;
    }
    output.imbue(std::locale::classic());
    output << graphFileHeader << '\n' << graph.vertexCount() << ' ' << graph.edgeCount() << '\n';
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
    auto size = validateGraphSize(graph.vertexCount(), graph.edgeCount());
    if (!size) {
        return size;
    }
    return writeFile(path, [&](std::ostream& output) {
        return writeGraph(graph, output);
    });
}
