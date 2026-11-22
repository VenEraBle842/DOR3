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
    EXPECT_EQ(loaded->vertex(0)->get().label, graph.vertex(0)->get().label);
    EXPECT_EQ(loaded->vertex(1)->get().label, "");
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
        EXPECT_EQ(result.error().code, ErrorCode::FileRead);
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
        return failure(ErrorCode::FileWrite);
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
    EXPECT_EQ(result.error().code, ErrorCode::LimitExceeded);
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
    EXPECT_EQ(result.error().code, ErrorCode::LimitExceeded);
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
    // Distribute edges across the graph to keep this boundary test sparse and fast.
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
    EXPECT_EQ(result.error().code, ErrorCode::LimitExceeded);
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
    GraphApplication application;
    ASSERT_TRUE(application.generate({8, Topology::Cycle, 0, 42}));
    ASSERT_TRUE(application.compareAlgorithms(3));
    ASSERT_EQ(application.measurements().size(), 2);
    for (std::size_t i = 0; i < 2; ++i) {
        const auto& row = application.measurements()[i];
        EXPECT_EQ(row.vertexCount, 8);
        EXPECT_EQ(row.edgeCount, 8);
        EXPECT_EQ(row.colorCount, 2);
        EXPECT_GE(row.medianMicroseconds, 0);
    }
    EXPECT_FALSE(application.compareAlgorithms(0));
    EXPECT_EQ(application.measurements().size(), 2);
    ASSERT_TRUE(application.compareSizeSeries({20, Topology::Path, 0, 42}, 4, 2));
    EXPECT_EQ(application.graph().vertexCount(), 8);
    ASSERT_EQ(application.measurements().size(), 8);
    EXPECT_EQ(application.measurements()[0].vertexCount, 5);
    EXPECT_EQ(application.measurements()[7].vertexCount, 20);
    EXPECT_EQ(application.measurements()[7].seed, 42);
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
    EXPECT_EQ(lines, 8);
    EXPECT_FALSE(application.compareSizeSeries({20, Topology::Cycle, 0, 1}, 20, 3));
    EXPECT_FALSE(application.compareSizeSeries({20, Topology::Path, 0, 1}, 0, 3));
    EXPECT_EQ(application.measurements().size(), 8);
    EXPECT_FALSE(application.compareAlgorithms(ApplicationLimits::maxRepetitionCount + 1));
    EXPECT_FALSE(application.compareSizeSeries({2000, Topology::Complete, 0, 1}, 1, 0));
    EXPECT_FALSE(application.compareSizeSeries({20, Topology::Path, 0, 1}, 4,
                                           ApplicationLimits::maxRepetitionCount + 1));
    EXPECT_EQ(application.measurements().size(), 8);
    DynamicArray<BenchmarkRow> empty;
    EXPECT_TRUE(exportBenchmark(empty, folder / "empty.csv"));
}
