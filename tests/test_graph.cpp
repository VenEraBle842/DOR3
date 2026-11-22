#include <coloring.hpp>
#include <generator.hpp>
#include <gtest/gtest.h>
#include <limits>

TEST(Graph, OwnsSymmetricEdgesAndRejectsInvalidChanges) {
    Graph graph;
    graph.addVertex({"A"});
    graph.addVertex({"B"});
    graph.addVertex({"C"});
    ASSERT_TRUE(graph.addEdge(0, 1));
    EXPECT_EQ(graph.edgeCount(), 1);
    EXPECT_TRUE(graph.hasEdge(1, 0));
    EXPECT_EQ(graph.neighbors(0)->get().size(), 1);
    EXPECT_EQ(graph.addEdge(1, 0).error().code, ErrorCode::DuplicateEdge);
    EXPECT_EQ(graph.addEdge(0, 0).error().code, ErrorCode::SelfLoop);
    EXPECT_EQ(graph.addEdge(0, 3).error().code, ErrorCode::InvalidVertex);
    EXPECT_FALSE(graph.vertex(3));
    EXPECT_FALSE(graph.neighbors(3));
    EXPECT_EQ(graph.edgeCount(), 1);
    ASSERT_TRUE(graph.removeEdge(1, 0));
    EXPECT_FALSE(graph.hasEdge(0, 1));
    EXPECT_EQ(graph.edgeCount(), 0);
    EXPECT_TRUE(graph.neighbors(0)->get().empty());
    EXPECT_EQ(graph.removeEdge(0, 1).error().code, ErrorCode::MissingEdge);
    EXPECT_EQ(graph.removeEdge(0, 5).error().code, ErrorCode::InvalidVertex);
}

TEST(Graph, SupportsArbitraryVertexDataAndMoveReuse) {
    UndirectedGraph<int> graph;
    for (int i = 0; i < 35; ++i) {
        graph.addVertex(i);
    }
    ASSERT_TRUE(graph.addEdge(1, 30));
    auto moved = std::move(graph);
    EXPECT_EQ(moved.vertex(30)->get(), 30);
    EXPECT_EQ(moved.edgeCount(), 1);
    EXPECT_EQ(graph.vertexCount(), 0);
    EXPECT_EQ(graph.edgeCount(), 0);
    graph.addVertex(42);
    EXPECT_EQ(graph.vertex(0)->get(), 42);
    auto coloring = colorGraph(moved, ColoringAlgorithm::LargestDegreeFirst);
    ASSERT_TRUE(coloring);
    EXPECT_TRUE(validateColoring(moved, *coloring));
    graph = std::move(moved);
    graph.addVertex(35);
    EXPECT_EQ(graph.vertexCount(), 36);
    EXPECT_TRUE(graph.hasEdge(30, 1));
}

TEST(Graph, RemovingMiddleNeighborPreservesOtherEdgesAndSymmetry) {
    Graph graph;
    for (int i = 0; i < 18; ++i) {
        graph.addVertex({std::to_string(i)});
    }
    for (std::size_t i = 1; i < 18; ++i) {
        ASSERT_TRUE(graph.addEdge(0, i));
    }
    ASSERT_TRUE(graph.removeEdge(0, 7));
    ASSERT_TRUE(graph.removeEdge(17, 0));
    EXPECT_EQ(graph.edgeCount(), 15);
    EXPECT_EQ(graph.neighbors(0)->get().size(), 15);
    for (std::size_t i = 1; i < 18; ++i) {
        EXPECT_EQ(graph.hasEdge(0, i), i != 7 && i != 17);
        EXPECT_EQ(graph.hasEdge(i, 0), i != 7 && i != 17);
    }
    ASSERT_TRUE(graph.addEdge(7, 0));
    ASSERT_TRUE(graph.addEdge(0, 17));
    EXPECT_EQ(graph.edgeCount(), 17);
    auto result = colorGraph(graph, ColoringAlgorithm::LargestDegreeFirst);
    ASSERT_TRUE(result);
    EXPECT_EQ(result->colorCount, 2);
    EXPECT_TRUE(validateColoring(graph, *result));
}

TEST(Coloring, KnownTopologiesAndEmptyGraph) {
    for (auto algorithm : {ColoringAlgorithm::Sequential, ColoringAlgorithm::LargestDegreeFirst}) {
        Graph empty;
        auto result = colorGraph(empty, algorithm);
        ASSERT_TRUE(result);
        EXPECT_EQ(result->colorCount, 0);
        EXPECT_TRUE(validateColoring(empty, *result));
        for (auto topology :
             {Topology::Path, Topology::Star, Topology::Complete, Topology::Cycle}) {
            auto graph = generateGraph({7, topology, 0, 100});
            ASSERT_TRUE(graph);
            auto colored = colorGraph(*graph, algorithm);
            ASSERT_TRUE(colored);
            EXPECT_TRUE(validateColoring(*graph, *colored));
            const auto expected = topology == Topology::Complete ? 7u
                                  : topology == Topology::Cycle  ? 3u
                                                                 : 2u;
            EXPECT_EQ(colored->colorCount, expected);
        }
        auto evenCycle = generateGraph({8, Topology::Cycle, 0, 100});
        EXPECT_EQ(colorGraph(*evenCycle, algorithm)->colorCount, 2);
        Graph isolated;
        isolated.addVertex({"alone"});
        isolated.addVertex({"also alone"});
        EXPECT_EQ(colorGraph(isolated, algorithm)->colorCount, 1);
    }
}

TEST(Coloring, DegreeOrderingAndTieBreakingAreDeterministic) {
    Graph graph;
    for (int i = 0; i < 4; ++i) {
        graph.addVertex({std::to_string(i)});
    }
    ASSERT_TRUE(graph.addEdge(0, 2));
    ASSERT_TRUE(graph.addEdge(2, 3));
    ASSERT_TRUE(graph.addEdge(3, 1));
    auto sequential = colorGraph(graph, ColoringAlgorithm::Sequential);
    auto degree = colorGraph(graph, ColoringAlgorithm::LargestDegreeFirst);
    ASSERT_TRUE(sequential);
    ASSERT_TRUE(degree);
    EXPECT_EQ(sequential->colorCount, 3);
    EXPECT_EQ(degree->colorCount, 2);
    EXPECT_EQ(degree->order[0], 2);
    EXPECT_EQ(degree->order[1], 3);
    EXPECT_EQ(degree->order[2], 0);
    EXPECT_EQ(degree->order[3], 1);
    EXPECT_FALSE(colorGraph(graph, static_cast<ColoringAlgorithm>(90)));
}

TEST(Coloring, ValidatorRejectsCorruptedColorsOrderAndCount) {
    auto graph = generateGraph({4, Topology::Path, 0, 1});
    auto result = colorGraph(*graph, ColoringAlgorithm::Sequential);
    result->colors[1] = result->colors[0];
    EXPECT_FALSE(validateColoring(*graph, *result));
    result = colorGraph(*graph, ColoringAlgorithm::Sequential);
    result->order[1] = result->order[0];
    EXPECT_FALSE(validateColoring(*graph, *result));
    result = colorGraph(*graph, ColoringAlgorithm::Sequential);
    result->colors[0] = std::numeric_limits<std::size_t>::max();
    EXPECT_FALSE(validateColoring(*graph, *result));
    result = colorGraph(*graph, ColoringAlgorithm::Sequential);
    result->colorCount = 3;
    EXPECT_FALSE(validateColoring(*graph, *result));
    result->colors.clear();
    EXPECT_FALSE(validateColoring(*graph, *result));
}

// Независимая проверка: перебор всех простых графов на пяти вершинах
TEST(Coloring, AllFiveVertexGraphsSatisfyEdgesAndDegreeBound) {
    for (unsigned mask = 0; mask < 1024; ++mask) {
        Graph graph;
        for (int i = 0; i < 5; ++i) {
            graph.addVertex({std::to_string(i)});
        }
        unsigned bit = 0;
        for (std::size_t i = 0; i < 5; ++i) {
            for (std::size_t j = i + 1; j < 5; ++j, ++bit) {
                if (mask & (1u << bit)) {
                    ASSERT_TRUE(graph.addEdge(i, j));
                }
            }
        }
        for (auto algorithm :
             {ColoringAlgorithm::Sequential, ColoringAlgorithm::LargestDegreeFirst}) {
            auto result = colorGraph(graph, algorithm);
            ASSERT_TRUE(result);
            std::size_t maximumDegree = 0;
            for (std::size_t i = 0; i < 5; ++i) {
                const auto& neighbors = graph.neighbors(i)->get();
                maximumDegree = std::max(maximumDegree, neighbors.size());
                for (std::size_t j = 0; j < neighbors.size(); ++j) {
                    ASSERT_NE(result->colors[i], result->colors[neighbors[j]]) << mask;
                }
            }
            EXPECT_LE(result->colorCount, maximumDegree + 1);
            EXPECT_TRUE(validateColoring(graph, *result));
        }
    }
}

TEST(Generator, ReproducibilityLimitsAndProbabilityExtremes) {
    auto first = generateGraph({35, Topology::Random, 0.3, 654});
    auto second = generateGraph({35, Topology::Random, 0.3, 654});
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    for (std::size_t i = 0; i < 35; ++i) {
        for (std::size_t j = 0; j < 35; ++j) {
            EXPECT_EQ(first->hasEdge(i, j), second->hasEdge(i, j));
        }
    }
    EXPECT_EQ(generateGraph({9, Topology::Random, 1, 100})->edgeCount(), 36);
    EXPECT_EQ(generateGraph({9, Topology::Random, 0, 100})->edgeCount(), 0);
    EXPECT_FALSE(generateGraph({2001, Topology::Path, 0, 100}));
    EXPECT_FALSE(generateGraph({2, Topology::Cycle, 0, 100}));
    EXPECT_FALSE(generateGraph({2, Topology::Random, -0.1, 100}));
    EXPECT_FALSE(
        generateGraph({2, Topology::Random, std::numeric_limits<double>::quiet_NaN(), 100}));
    EXPECT_FALSE(generateGraph({2, static_cast<Topology>(99), 0, 100}));
}
