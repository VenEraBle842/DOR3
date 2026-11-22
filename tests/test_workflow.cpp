#include <application.hpp>
#include <chrono>
#include <fstream>
#include <gtest/gtest.h>
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
    EXPECT_FALSE(std::filesystem::exists(path.string() + ".tmp"));
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
    std::ifstream preserved(path);
    preserved >> text;
    EXPECT_EQ(text, "original");
    EXPECT_TRUE(saveGraph(Graph{}, path));
    EXPECT_TRUE(loadGraph(path));
}

TEST_F(FilesTest, ApplicationPreservesStateOnFailuresAndInvalidatesColoringOnEdits) {
    GraphApplication application;
    ASSERT_TRUE(application.generate({4, Topology::Cycle, 0, 1}));
    ASSERT_TRUE(application.run(ColoringAlgorithm::Sequential));
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
    ASSERT_TRUE(application.run(ColoringAlgorithm::LargestDegreeFirst));
    ASSERT_TRUE(application.removeEdge(0, 1));
    EXPECT_FALSE(application.coloring());
    EXPECT_FALSE(application.addVertex("invalid\nlabel"));
    application.clear();
    EXPECT_EQ(application.graph().vertexCount(), 0);
}

TEST_F(FilesTest, ComparisonAndCsvAndFailedComparisonPreserveResults) {
    GraphApplication application;
    ASSERT_TRUE(application.generate({8, Topology::Cycle, 0, 42}));
    ASSERT_TRUE(application.compare(3));
    ASSERT_EQ(application.measurements().size(), 2);
    for (std::size_t i = 0; i < 2; ++i) {
        const auto& row = application.measurements()[i];
        EXPECT_EQ(row.vertices, 8);
        EXPECT_EQ(row.edges, 8);
        EXPECT_EQ(row.colors, 2);
        EXPECT_GE(row.medianMicroseconds, 0);
        EXPECT_TRUE(row.verified);
    }
    EXPECT_FALSE(application.compare(0));
    EXPECT_EQ(application.measurements().size(), 2);
    ASSERT_TRUE(application.compareSeries({20, Topology::Path, 0, 42}, 4, 2));
    EXPECT_EQ(application.graph().vertexCount(), 8);
    ASSERT_EQ(application.measurements().size(), 8);
    EXPECT_EQ(application.measurements()[0].vertices, 5);
    EXPECT_EQ(application.measurements()[7].vertices, 20);
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
    EXPECT_FALSE(application.compareSeries({20, Topology::Cycle, 0, 1}, 20, 3));
    EXPECT_FALSE(application.compareSeries({20, Topology::Path, 0, 1}, 0, 3));
    EXPECT_EQ(application.measurements().size(), 8);
    DynamicArray<BenchmarkRow> empty;
    EXPECT_TRUE(exportBenchmark(empty, folder / "empty.csv"));
}
