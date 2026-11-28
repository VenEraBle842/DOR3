#include <benchmark.hpp>
#include <chrono>
#include <iomanip>
#include <application_limits.hpp>
#include <limits>
#include <locale>
#include <ostream>
#include <storage.hpp>
#include <string_view>

static void writeCsvField(std::ostream& output, std::string_view value) {
    if (value.find_first_of(",\"\r\n") == std::string_view::npos) {
        output << value;
        return;
    }
    output << '"';
    for (const auto character : value) {
        if (character == '"') output << '"';
        output << character;
    }
    output << '"';
}

static Result<void> validateRepetitionCount(std::size_t repetitionCount) {
    if (repetitionCount == 0 || repetitionCount > ApplicationLimits::maxRepetitionCount) {
        return failure(GraphErrorCode::InvalidParameters,
                       "Repetitions must be 1.." +
                           std::to_string(ApplicationLimits::maxRepetitionCount) + ".");
    }
    return {};
}

Result<ArraySequence<BenchmarkResult>> compareAlgorithms(const Graph& graph, std::size_t repetitionCount) {
    auto repetitionValidation = validateRepetitionCount(repetitionCount);
    if (!repetitionValidation) {
        return std::unexpected(repetitionValidation.error());
    }
    ArraySequence<BenchmarkResult> rows;
    for (const auto& algorithm : coloringAlgorithmCatalog<Graph>) {
        auto warmup = algorithm.colorGraph(graph);
        if (!warmup) {
            return std::unexpected(warmup.error());
        }
        auto valid = validateColoring(graph, *warmup);
        if (!valid) {
            return std::unexpected(valid.error());
        }
        ArraySequence<double> samples;
        for (std::size_t iteration = 0; iteration < repetitionCount; ++iteration) {
            const auto start = std::chrono::steady_clock::now();
            auto result = algorithm.colorGraph(graph);
            const auto stop = std::chrono::steady_clock::now();
            if (!result) {
                return std::unexpected(result.error());
            }
            auto checked = validateColoring(graph, *result);
            if (!checked) {
                return std::unexpected(checked.error());
            }
            auto appended = samples.push_back(std::chrono::duration<double, std::micro>(stop - start).count());
            if (!appended) return failure(appended.error());
        }
        heapSort(samples);
        const auto middle = repetitionCount / 2;
        const double median =
            repetitionCount % 2 ? samples[middle] : (samples[middle - 1] + samples[middle]) / 2;
        auto appended = rows.push_back({graph.vertexCount(),
                        graph.edgeCount(),
                        algorithm.id,
                        median,
                        warmup->colorCount,
                        repetitionCount,
                        "Current graph",
                        0,
                        0});
        if (!appended) return failure(appended.error());
    }
    return rows;
}

Result<ArraySequence<BenchmarkResult>>
compareSizeSeries(const GenerationParameters& parameters,
                  std::size_t seriesPointCount,
                  std::size_t repetitionCount) {
    auto repetitionValidation = validateRepetitionCount(repetitionCount);
    if (!repetitionValidation) {
        return std::unexpected(repetitionValidation.error());
    }
    auto validParameters = validateGenerationParameters(parameters);
    if (!validParameters) {
        return std::unexpected(validParameters.error());
    }
    if (seriesPointCount == 0 || seriesPointCount > ApplicationLimits::maxSeriesPointCount ||
        parameters.vertexCount < seriesPointCount) {
        return failure(GraphErrorCode::InvalidParameters,
                       "Use 1.." + std::to_string(ApplicationLimits::maxSeriesPointCount) +
                           " points and a maximum size between points and " +
                           std::to_string(ApplicationLimits::maxGeneratedVertices) + ".");
    }
    auto firstParameters = parameters;
    firstParameters.vertexCount = parameters.vertexCount / seriesPointCount;
    auto firstValidation = validateGenerationParameters(firstParameters);
    if (!firstValidation) {
        return std::unexpected(firstValidation.error());
    }
    ArraySequence<BenchmarkResult> rows;
    for (std::size_t point = 1; point <= seriesPointCount; ++point) {
        auto current = parameters;
        current.vertexCount = parameters.vertexCount * point / seriesPointCount;
        auto graph = generateGraph(current);
        if (!graph) {
            return std::unexpected(graph.error());
        }
        auto comparison = compareAlgorithms(*graph, repetitionCount);
        if (!comparison) {
            return std::unexpected(comparison.error());
        }
        for (std::size_t i = 0; i < comparison->size(); ++i) {
            auto row = std::move((*comparison)[i]);
            row.topology = topologyName(parameters.topology);
            row.seed = parameters.seed;
            row.edgeProbability = parameters.edgeProbability;
            auto appended = rows.push_back(std::move(row));
            if (!appended) return failure(appended.error());
        }
    }
    return rows;
}

Result<void> exportBenchmark(const ArraySequence<BenchmarkResult>& rows,
                             const std::filesystem::path& path) {
    return writeFile(path, [&](std::ostream& output) -> Result<void> {
        output.imbue(std::locale::classic());
        output << "Vertices,Edges,Algorithm,MedianMicroseconds,Colors,Repetitions,Verified,"
                  "Topology,Seed,Probability\n";
        output << std::setprecision(std::numeric_limits<double>::max_digits10);
        // Результаты сравнения публикуются только после независимой проверки
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const auto& row = rows[i];
            output << row.vertexCount << ',' << row.edgeCount << ',';
            writeCsvField(output, coloringAlgorithmName(row.algorithm));
            output << ',' << row.medianMicroseconds << ',' << row.colorCount << ','
                   << row.repetitionCount << ",true,";
            writeCsvField(output, row.topology);
            output << ',' << row.seed << ',' << row.edgeProbability << '\n';
        }
        if (!output) {
            return failure(GraphErrorCode::FileWrite);
        }
        return {};
    });
}
