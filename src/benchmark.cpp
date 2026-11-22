#include <benchmark.hpp>
#include <chrono>
#include <iomanip>
#include <locale>
#include <ostream>
#include <storage.hpp>

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

Result<DynamicArray<BenchmarkRow>> compareAlgorithms(const Graph& graph, std::size_t repetitions) {
    if (repetitions == 0 || repetitions > 101) {
        return failure(ErrorCode::InvalidParameters, "Repetitions must be 1..101.");
    }
    DynamicArray<BenchmarkRow> rows;
    for (auto algorithm : {ColoringAlgorithm::Sequential, ColoringAlgorithm::LargestDegreeFirst}) {
        auto warmup = colorGraph(graph, algorithm);
        if (!warmup) {
            return std::unexpected(warmup.error());
        }
        auto valid = validateColoring(graph, *warmup);
        if (!valid) {
            return std::unexpected(valid.error());
        }
        DynamicArray<double> samples;
        for (std::size_t iteration = 0; iteration < repetitions; ++iteration) {
            const auto start = std::chrono::steady_clock::now();
            auto result = colorGraph(graph, algorithm);
            const auto stop = std::chrono::steady_clock::now();
            if (!result) {
                return std::unexpected(result.error());
            }
            auto checked = validateColoring(graph, *result);
            if (!checked) {
                return std::unexpected(checked.error());
            }
            samples.push_back(std::chrono::duration<double, std::micro>(stop - start).count());
        }
        heapSort(samples);
        const auto middle = repetitions / 2;
        const double median =
            repetitions % 2 ? samples[middle] : (samples[middle - 1] + samples[middle]) / 2;
        rows.push_back({graph.vertexCount(),
                        graph.edgeCount(),
                        algorithm,
                        median,
                        warmup->colorCount,
                        repetitions,
                        true,
                        "Current graph",
                        0,
                        0});
    }
    return rows;
}

Result<DynamicArray<BenchmarkRow>>
compareSizes(const GenerationParameters& parameters, std::size_t points, std::size_t repetitions) {
    if (points == 0 || points > 20 || parameters.vertexCount < points ||
        parameters.vertexCount > 2000) {
        return failure(ErrorCode::InvalidParameters,
                       "Use 1..20 points and a maximum size between points and 2000.");
    }
    if (parameters.topology == Topology::Cycle && parameters.vertexCount / points < 3) {
        return failure(ErrorCode::InvalidParameters,
                       "Each cycle in the series needs at least 3 vertices.");
    }
    DynamicArray<BenchmarkRow> rows;
    for (std::size_t point = 1; point <= points; ++point) {
        auto current = parameters;
        current.vertexCount = parameters.vertexCount * point / points;
        auto graph = generateGraph(current);
        if (!graph) {
            return std::unexpected(graph.error());
        }
        auto comparison = compareAlgorithms(*graph, repetitions);
        if (!comparison) {
            return std::unexpected(comparison.error());
        }
        for (std::size_t i = 0; i < comparison->size(); ++i) {
            auto row = std::move((*comparison)[i]);
            row.topology = topologyName(parameters.topology);
            row.seed = parameters.seed;
            row.probability = parameters.probability;
            rows.push_back(std::move(row));
        }
    }
    return rows;
}

Result<void> exportBenchmark(const DynamicArray<BenchmarkRow>& rows,
                             const std::filesystem::path& path) {
    return writeFile(path, [&](std::ostream& output) -> Result<void> {
        output.imbue(std::locale::classic());
        output << "Vertices,Edges,Algorithm,MedianMicroseconds,Colors,Repetitions,Verified,"
                  "Topology,Seed,Probability\n";
        output << std::setprecision(17);
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const auto& row = rows[i];
            output << row.vertices << ',' << row.edges << ',' << algorithmName(row.algorithm) << ','
                   << row.medianMicroseconds << ',' << row.colors << ',' << row.repetitions << ','
                   << (row.verified ? "true" : "false") << ',' << row.topology << ',' << row.seed
                   << ',' << row.probability << '\n';
        }
        if (!output) {
            return failure(ErrorCode::FileWrite);
        }
        return {};
    });
}
