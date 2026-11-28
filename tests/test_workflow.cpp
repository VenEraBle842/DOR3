#include <application.hpp>
#include <chrono>
#include <fstream>
#include <gtest/gtest.h>
#include <application_limits.hpp>
#include <limits>
#include <sstream>
#include <storage.hpp>

class FilesTest : public testing::Test {
protected:
    std::filesystem::path folder;

    void SetUp() override {
        folder = std::filesystem::temp_directory_path() /
                 ("dor3-tests-" +
                  std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ASSERT_TRUE(std::filesystem::create_directory(folder));
    }

    void TearDown() override {
        std::filesystem::remove_all(folder);
    }

    void expectNoTemporaryDirectories() const {
        for (const auto& entry : std::filesystem::directory_iterator(folder)) {
            EXPECT_FALSE(entry.path().filename().string().starts_with(".dor3-write-"))
                << entry.path();
        }
    }
};

TEST(Storage, RoundTripQuotedLabelsAndEdges) {
    Graph graph;
    graph.addVertex({"Аня: \"тест\" \\"});
    graph.addVertex({""});
    ASSERT_TRUE(graph.addEdge(0, 1));
    std::stringstream text;
    ASSERT_TRUE(writeGraph(graph, text));
    auto loaded = readGraph(text);
    ASSERT_TRUE(loaded);
    EXPECT_EQ(loaded->vertex(0)->get().data.label, graph.vertex(0)->get().data.label);
    EXPECT_EQ(loaded->vertex(1)->get().data.label, "");
    EXPECT_EQ(loaded->edgeCount(), 1);
    EXPECT_TRUE(loaded->hasEdge(1, 0));
}

TEST(Storage, RejectsMalformedAndTruncatedInput) {
    for (const auto* text : {"",
                             "DOR3_GRAPH 2\n0 0\n",
                             "DOR3_GRAPH 1\n-1 0\n",
                             "DOR3_GRAPH 1\n2 1\n\"a\"\n\"b\"\n0 0\n",
                             "DOR3_GRAPH 1\n2 1\n\"a\"\n\"b\"\n0 2\n",
                             "DOR3_GRAPH 1\n1 0\nnot quoted\n",
                             "DOR3_GRAPH 1\n1 0\n\"unclosed\n",
                             "DOR3_GRAPH 1\n0 0\nextra\n",
                             "DOR3_GRAPH 1\n1 0 extra\n",
                             "DOR3_GRAPH 1\n1 0\n\"a\rb\"\n",
                             "DOR3_GRAPH 1\n2 2\n\"a\"\n\"b\"\n0 1\n1 0\n",
                             "DOR3_GRAPH 1\n1 1\n\"a\"\n"}) {
        std::istringstream input(text);
        EXPECT_FALSE(readGraph(input)) << text;
    }
    std::istringstream crlf("DOR3_GRAPH 1\r\n1 0\r\n\"test\"\r\n");
    EXPECT_TRUE(readGraph(crlf));
    Graph graph;
    std::ostringstream broken;
    broken.setstate(std::ios::badbit);
    EXPECT_FALSE(writeGraph(graph, broken));
}

TEST(Storage, RejectsSignedOverflowingAndPartialNumbersInEveryField) {
    const auto maximum = std::to_string(std::numeric_limits<std::size_t>::max());
    const DynamicArray<std::string> invalidTokens{
        "-1", "-" + maximum, "+1", maximum + "0", "1x", "1.0"};
    for (const auto& token : invalidTokens) {
        const DynamicArray<std::string> files{
            "DOR3_GRAPH 1\n" + token + " 0\n\"a\"\n",
            "DOR3_GRAPH 1\n2 " + token + "\n\"a\"\n\"b\"\n0 1\n",
            "DOR3_GRAPH 1\n2 1\n\"a\"\n\"b\"\n" + token + " 0\n",
            "DOR3_GRAPH 1\n2 1\n\"a\"\n\"b\"\n0 " + token + "\n"};
        for (const auto& file : files) {
            std::istringstream input(file);
            EXPECT_FALSE(readGraph(input)) << file;
        }
    }
    for (const auto* file : {"DOR3_GRAPH 1\n-0 0\n",
                             "DOR3_GRAPH 1\n0 -0\n",
                             "DOR3_GRAPH 1\n2 1\n\"a\"\n\"b\"\n-0 1\n",
                             "DOR3_GRAPH 1\n2 1\n\"a\"\n\"b\"\n1 -0\n",
                             "DOR3_GRAPH 1\n2 1\n\"a\"\n\"b\"\n0 1 extra\n"}) {
        std::istringstream input(file);
        EXPECT_FALSE(readGraph(input)) << file;
    }
}

TEST(Storage, SharedLimitsAcceptBoundariesAndRejectOversizedHeaders) {
    EXPECT_TRUE(validateGraphLimits(ApplicationLimits::maxVertices, ApplicationLimits::maxEdges));
    EXPECT_FALSE(validateGraphLimits(ApplicationLimits::maxVertices + 1, 0));
    EXPECT_FALSE(validateGraphLimits(0, ApplicationLimits::maxEdges + 1));
    const DynamicArray<std::string> counts{
        std::to_string(ApplicationLimits::maxVertices + 1) + " 0",
        "0 " + std::to_string(ApplicationLimits::maxEdges + 1)};
    for (const auto& line : counts) {
        std::istringstream input("DOR3_GRAPH 1\n" + line + "\n");
        auto result = readGraph(input);
        ASSERT_FALSE(result);
        EXPECT_EQ(result.error().code, GraphErrorCode::FileRead);
    }
}

TEST_F(FilesTest, FailedWriteKeepsExistingTarget) {
    const auto path = folder / "graph.data";
    {
        std::ofstream file(path);
        file << "original";
    }
    auto result = writeFile(path, [](std::ostream& output) -> Result<void> {
        output << "partial";
        return failure(GraphErrorCode::FileWrite);
    });
    EXPECT_FALSE(result);
    std::ifstream input(path);
    std::string text;
    input >> text;
    EXPECT_EQ(text, "original");
    expectNoTemporaryDirectories();
    EXPECT_FALSE(saveGraph(Graph{}, folder / "missing" / "graph.data"));
    std::size_t entries = 0;
    for (const auto& entry : std::filesystem::directory_iterator(folder)) {
        (void)entry;
        ++entries;
    }
    EXPECT_EQ(entries, 1);
    Graph invalid;
    invalid.addVertex({"invalid\nlabel"});
    EXPECT_FALSE(saveGraph(invalid, path));
    expectNoTemporaryDirectories();
    std::ifstream preserved(path);
    preserved >> text;
    EXPECT_EQ(text, "original");
    EXPECT_TRUE(saveGraph(Graph{}, path));
    EXPECT_TRUE(loadGraph(path));
    expectNoTemporaryDirectories();
}

TEST_F(FilesTest, OversizedGraphCannotBeWrittenOrReplaceExistingFile) {
    Graph graph;
    for (std::size_t i = 0; i <= ApplicationLimits::maxVertices; ++i) {
        graph.addVertex({""});
    }
    std::ostringstream output;
    auto result = writeGraph(graph, output);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, GraphErrorCode::LimitExceeded);
    EXPECT_TRUE(output.str().empty());

    const auto path = folder / "graph.data";
    ASSERT_TRUE(saveGraph(Graph{}, path));
    EXPECT_FALSE(saveGraph(graph, path));
    auto loaded = loadGraph(path);
    ASSERT_TRUE(loaded);
    EXPECT_EQ(loaded->vertexCount(), 0);
    expectNoTemporaryDirectories();
}

TEST_F(FilesTest, ApplicationVertexLimitPreservesSavedGraphAndColoring) {
    GraphApplication application;
    for (std::size_t i = 0; i < ApplicationLimits::maxVertices; ++i) {
        ASSERT_TRUE(application.addVertex(""));
    }
    const auto path = folder / "boundary.graph";
    ASSERT_TRUE(application.save(path));
    ASSERT_TRUE(application.computeColoring(ColoringAlgorithm::Sequential));
    auto result = application.addVertex("too many");
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, GraphErrorCode::LimitExceeded);
    EXPECT_EQ(application.graph().vertexCount(), ApplicationLimits::maxVertices);
    EXPECT_FALSE(application.isModified());
    EXPECT_TRUE(application.coloring());
    ASSERT_TRUE(application.open(path));
    EXPECT_EQ(application.graph().vertexCount(), ApplicationLimits::maxVertices);
}

TEST(GraphApplication, EdgeLimitRejectsNewEdgeWithoutInvalidatingColoring) {
    GraphApplication application;
    for (std::size_t i = 0; i < ApplicationLimits::maxVertices; ++i) {
        ASSERT_TRUE(application.addVertex(""));
    }
    // Рёбра распределены по вершинам, чтобы граничный тест оставался разреженным.
    constexpr auto neighborsPerVertex = ApplicationLimits::maxEdges / ApplicationLimits::maxVertices + 1;
    for (VertexId first = 0; first < ApplicationLimits::maxVertices &&
                             application.graph().edgeCount() < ApplicationLimits::maxEdges; ++first) {
        for (std::size_t offset = 1; offset <= neighborsPerVertex &&
                                     application.graph().edgeCount() < ApplicationLimits::maxEdges;
             ++offset) {
            ASSERT_TRUE(application.addEdge(first, (first + offset) % ApplicationLimits::maxVertices));
        }
    }
    ASSERT_EQ(application.graph().edgeCount(), ApplicationLimits::maxEdges);
    ASSERT_TRUE(application.computeColoring(ColoringAlgorithm::Sequential));
    constexpr VertexId target = neighborsPerVertex + 1;
    ASSERT_FALSE(application.graph().hasEdge(0, target));
    auto result = application.addEdge(0, target);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, GraphErrorCode::LimitExceeded);
    EXPECT_EQ(application.graph().edgeCount(), ApplicationLimits::maxEdges);
    EXPECT_FALSE(application.graph().hasEdge(0, target));
    EXPECT_FALSE(application.graph().hasEdge(target, 0));
    EXPECT_TRUE(application.coloring());
}

TEST_F(FilesTest, ApplicationPreservesStateOnFailuresAndInvalidatesColoringOnEdits) {
    GraphApplication application;
    ASSERT_TRUE(application.generate({4, Topology::Cycle, 0, 1}));
    ASSERT_TRUE(application.computeColoring(ColoringAlgorithm::Sequential));
    EXPECT_TRUE(application.isModified());
    ASSERT_TRUE(application.save(folder / "граф.data"));
    EXPECT_FALSE(application.isModified());
    EXPECT_FALSE(application.open(folder / "missing"));
    EXPECT_EQ(application.graph().edgeCount(), 4);
    EXPECT_TRUE(application.coloring());
    EXPECT_FALSE(application.generate({2, Topology::Cycle, 0, 1}));
    EXPECT_TRUE(application.coloring());
    EXPECT_FALSE(application.addEdge(0, 0));
    EXPECT_TRUE(application.coloring());
    ASSERT_TRUE(application.addVertex("isolated"));
    EXPECT_FALSE(application.coloring());
    EXPECT_TRUE(application.isModified());
    ASSERT_TRUE(application.open(folder / "граф.data"));
    EXPECT_EQ(application.graph().vertexCount(), 4);
    EXPECT_FALSE(application.isModified());
    ASSERT_TRUE(application.computeColoring(ColoringAlgorithm::LargestDegreeFirst));
    ASSERT_TRUE(application.removeEdge(0, 1));
    EXPECT_FALSE(application.coloring());
    EXPECT_FALSE(application.addVertex("invalid\nlabel"));
    application.clear();
    EXPECT_EQ(application.graph().vertexCount(), 0);
}

TEST_F(FilesTest, ComparisonAndCsvAndFailedComparisonPreserveResults) {
    const auto algorithmCount = std::size(coloringAlgorithmCatalog<Graph>);
    constexpr std::size_t sizeCount = 4;
    const auto rowCount = algorithmCount * sizeCount;
    GraphApplication application;
    ASSERT_TRUE(application.generate({8, Topology::Cycle, 0, 42}));
    ASSERT_TRUE(application.compareAlgorithms(3));
    ASSERT_EQ(application.measurements().size(), algorithmCount);
    for (std::size_t i = 0; i < algorithmCount; ++i) {
        const auto& row = application.measurements()[i];
        EXPECT_EQ(row.vertexCount, 8);
        EXPECT_EQ(row.edgeCount, 8);
        EXPECT_EQ(row.colorCount, 2);
        EXPECT_GE(row.medianMicroseconds, 0);
    }
    EXPECT_FALSE(application.compareAlgorithms(0));
    EXPECT_EQ(application.measurements().size(), algorithmCount);
    ASSERT_TRUE(application.compareSizeSeries({20, Topology::Path, 0, 42}, sizeCount, 2));
    EXPECT_EQ(application.graph().vertexCount(), 8);
    ASSERT_EQ(application.measurements().size(), rowCount);
    EXPECT_EQ(application.measurements()[0].vertexCount, 5);
    EXPECT_EQ(application.measurements()[rowCount - 1].vertexCount, 20);
    EXPECT_EQ(application.measurements()[rowCount - 1].seed, 42);
    ASSERT_TRUE(exportBenchmark(application.measurements(), folder / "results.csv"));
    std::ifstream input(folder / "results.csv");
    std::string line;
    std::getline(input, line);
    EXPECT_EQ(line,
              "Vertices,Edges,Algorithm,MedianMicroseconds,Colors,Repetitions,Verified,Topology,"
              "Seed,Probability");
    std::size_t lines = 0;
    while (std::getline(input, line)) {
        EXPECT_NE(line.find(",true,"), std::string::npos);
        ++lines;
    }
    EXPECT_EQ(lines, rowCount);
    EXPECT_FALSE(application.compareSizeSeries({20, Topology::Cycle, 0, 1}, 20, 3));
    EXPECT_FALSE(application.compareSizeSeries({20, Topology::Path, 0, 1}, 0, 3));
    EXPECT_EQ(application.measurements().size(), rowCount);
    EXPECT_FALSE(application.compareAlgorithms(ApplicationLimits::maxRepetitionCount + 1));
    EXPECT_FALSE(application.compareSizeSeries({2000, Topology::Complete, 0, 1}, 1, 0));
    EXPECT_FALSE(application.compareSizeSeries({20, Topology::Path, 0, 1}, 4,
                                           ApplicationLimits::maxRepetitionCount + 1));
    EXPECT_EQ(application.measurements().size(), rowCount);
    DynamicArray<BenchmarkRow> empty;
    EXPECT_TRUE(exportBenchmark(empty, folder / "empty.csv"));
}

TEST(Storage, GappedVertexAndEdgeIdsAndDeletedHighWaterMarksRoundTrip) {
    Graph graph;
    for (int i = 0; i < 6; ++i) graph.addVertex({"label " + std::to_string(i)});
    const auto a = *graph.addEdge(0, 2);
    const auto b = *graph.addEdge(2, 4);
    const auto c = *graph.addEdge(4, 5);
    ASSERT_TRUE(graph.removeVertex(1));
    ASSERT_TRUE(graph.removeVertex(3));
    ASSERT_TRUE(graph.removeEdge(a));
    ASSERT_TRUE(graph.removeEdge(c));
    ASSERT_TRUE(graph.removeVertex(5));
    std::stringstream text;
    ASSERT_TRUE(writeGraph(graph, text));
    EXPECT_TRUE(text.str().starts_with("DOR3_GRAPH 2\n"));
    auto loaded = readGraph(text);
    ASSERT_TRUE(loaded);
    ASSERT_EQ(loaded->vertexCount(), 3);
    EXPECT_TRUE(loaded->containsVertex(0));
    EXPECT_TRUE(loaded->containsVertex(2));
    EXPECT_TRUE(loaded->containsVertex(4));
    EXPECT_FALSE(loaded->containsVertex(1));
    EXPECT_EQ(loaded->vertex(4)->get().data.label, "label 4");
    EXPECT_EQ(*loaded->edgeId(4, 2), b);
    EXPECT_EQ(loaded->addVertex({"new"}), 6);
    EXPECT_GT(*loaded->addEdge(0, 6), c);
    auto coloring = colorGraph(*loaded, ColoringAlgorithm::LargestDegreeFirst);
    ASSERT_TRUE(coloring);
    EXPECT_TRUE(validateColoring(*loaded, *coloring));
}

TEST(Storage, PreservesEmptyClearedAndExhaustedIdentifierState) {
    Graph graph;
    graph.addVertex({""});
    graph.addVertex({""});
    ASSERT_TRUE(graph.addEdge(0, 1));
    graph.clear();
    std::stringstream text;
    ASSERT_TRUE(writeGraph(graph, text));
    auto loaded = readGraph(text);
    ASSERT_TRUE(loaded);
    EXPECT_EQ(loaded->addVertex({""}), 2);
    EXPECT_EQ(loaded->addVertex({""}), 3);
    EXPECT_EQ(*loaded->addEdge(2, 3), 1);
    Graph exhausted;
    constexpr auto maximum = std::numeric_limits<std::size_t>::max();
    ASSERT_TRUE(exhausted.addVertexWithId(maximum, {"last"}));
    ASSERT_TRUE(exhausted.advanceIdCounters({maximum, true}, {maximum, true}));
    std::stringstream exhaustedText;
    ASSERT_TRUE(writeGraph(exhausted, exhaustedText));
    auto restored = readGraph(exhaustedText);
    ASSERT_TRUE(restored);
    EXPECT_TRUE(restored->containsVertex(maximum));
    EXPECT_THROW(restored->addVertex({""}), std::overflow_error);
    EXPECT_TRUE(restored->edgeIdCounter().exhausted);
}

TEST(Storage, VersionTwoRejectsInvalidIdsOrderingAndHighWaterMarks) {
    for (const auto* file : {
             "DOR3_GRAPH 2\n2 0 4 0 0 0\n2 \"a\"\n2 \"duplicate\"\n",
             "DOR3_GRAPH 2\n2 0 4 0 0 0\n3 \"a\"\n2 \"unordered\"\n",
             "DOR3_GRAPH 2\n1 0 2 0 0 0\n3 \"a\"\n",
             "DOR3_GRAPH 2\n0 0 2 1 0 0\n",
             "DOR3_GRAPH 2\n0 0 0 0 0 2\n",
             "DOR3_GRAPH 2\n0 0 0 0 0 0 extra\n",
             "DOR3_GRAPH 2\n1 0 4 0 0 0\n-1 \"a\"\n",
             "DOR3_GRAPH 2\n1 0 4 0 0 0\n1 \"a\" extra\n",
             "DOR3_GRAPH 2\n2 1 4 0 2 0\n1 \"a\"\n3 \"b\"\n2 1 3\n",
             "DOR3_GRAPH 2\n3 2 5 0 9 0\n0 \"a\"\n2 \"b\"\n4 \"c\"\n8 0 2\n7 2 4\n",
             "DOR3_GRAPH 2\n2 1 3 0 1 0\n0 \"a\"\n2 \"b\"\n0 0 1\n"}) {
        std::istringstream input(file);
        EXPECT_FALSE(readGraph(input)) << file;
    }
}

TEST_F(FilesTest, ApplicationRemovesVertexInvalidatesOnlyOnSuccessAndSavesGaps) {
    GraphApplication app;
    ASSERT_TRUE(app.generate({6, Topology::Path, 0, 1}));
    ASSERT_TRUE(app.computeColoring(ColoringAlgorithm::Sequential));
    ASSERT_TRUE(app.save(folder / "before.graph"));
    EXPECT_FALSE(app.removeVertex(99));
    EXPECT_TRUE(app.coloring());
    EXPECT_FALSE(app.isModified());
    ASSERT_TRUE(app.removeVertex(2));
    EXPECT_FALSE(app.coloring());
    EXPECT_TRUE(app.isModified());
    EXPECT_FALSE(app.graph().containsVertex(2));
    EXPECT_EQ(app.graph().vertexCount(), 5);
    EXPECT_EQ(app.graph().edgeCount(), 3);
    ASSERT_TRUE(app.computeColoring(ColoringAlgorithm::LargestDegreeFirst));
    EXPECT_TRUE(validateColoring(app.graph(), *app.coloring()));
    ASSERT_TRUE(app.compareAlgorithms(1));
    ASSERT_TRUE(app.save(folder / "gapped.graph"));
    ASSERT_TRUE(app.open(folder / "gapped.graph"));
    EXPECT_FALSE(app.graph().containsVertex(2));
    EXPECT_EQ(*app.addVertex("new"), 6);
    app.clear();
    EXPECT_EQ(*app.addVertex("after clear"), 7);
}
