#include <coloring.hpp>
#include <generator.hpp>
#include <gtest/gtest.h>
#include <limits>
#include <stdexcept>

namespace {
struct ThrowingMoveConstructor {
    ThrowingMoveConstructor() = default;
    ThrowingMoveConstructor(ThrowingMoveConstructor&&) noexcept(false) {}
    ThrowingMoveConstructor& operator=(ThrowingMoveConstructor&&) noexcept = default;
};

struct ThrowingMoveAssignment {
    ThrowingMoveAssignment() = default;
    ThrowingMoveAssignment(ThrowingMoveAssignment&&) noexcept = default;
    ThrowingMoveAssignment& operator=(ThrowingMoveAssignment&&) noexcept(false) {
        return *this;
    }
};

template <class T>
concept AcceptedVertexData = requires { typename UndirectedGraph<T>; };

static_assert(AcceptedVertexData<VertexData>);
static_assert(AcceptedVertexData<int>);
static_assert(AcceptedVertexData<std::string>);
static_assert(!AcceptedVertexData<ThrowingMoveConstructor>);
static_assert(!AcceptedVertexData<ThrowingMoveAssignment>);

struct ThrowingDefaultData {
    inline static int defaultsBeforeFailure = -1;
    int value = 0;

    ThrowingDefaultData() {
        if (defaultsBeforeFailure == 0) {
            throw std::runtime_error("Cannot construct vertex storage");
        }
        if (defaultsBeforeFailure > 0) {
            --defaultsBeforeFailure;
        }
    }
    explicit ThrowingDefaultData(int initialValue) : value(initialValue) {}
    ThrowingDefaultData(ThrowingDefaultData&&) noexcept = default;
    ThrowingDefaultData& operator=(ThrowingDefaultData&&) noexcept = default;
};
}

TEST(Graph, StorageConstructionFailurePreservesVerticesAndEdges) {
    UndirectedGraph<ThrowingDefaultData> graph;
    for (int i = 0; i < 4; ++i) {
        graph.addVertex(ThrowingDefaultData(i));
    }
    ASSERT_TRUE(graph.addEdge(0, 1));
    ASSERT_TRUE(graph.addEdge(1, 3));

    ThrowingDefaultData::defaultsBeforeFailure = 2;
    EXPECT_THROW(graph.addVertex(ThrowingDefaultData(4)), std::runtime_error);
    ThrowingDefaultData::defaultsBeforeFailure = -1;

    EXPECT_EQ(graph.vertexCount(), 4);
    EXPECT_EQ(graph.edgeCount(), 2);
    for (VertexId i = 0; i < 4; ++i) {
        EXPECT_EQ(graph.vertex(i)->get().value, static_cast<int>(i));
    }
    EXPECT_TRUE(graph.hasEdge(0, 1));
    EXPECT_TRUE(graph.hasEdge(1, 0));
    EXPECT_TRUE(graph.hasEdge(1, 3));
    EXPECT_TRUE(graph.hasEdge(3, 1));
    EXPECT_EQ(graph.addVertex(ThrowingDefaultData(4)), 4);
    EXPECT_EQ(graph.vertexCount(), 5);
    EXPECT_TRUE(graph.hasEdge(0, 1));
}

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
    for (const auto& entry : coloringAlgorithms<Graph>) {
        const auto algorithm = entry.id;
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
    EXPECT_EQ(degree->processingOrder[0], 2);
    EXPECT_EQ(degree->processingOrder[1], 3);
    EXPECT_EQ(degree->processingOrder[2], 0);
    EXPECT_EQ(degree->processingOrder[3], 1);
    EXPECT_FALSE(colorGraph(graph, static_cast<ColoringAlgorithm>(90)));
}

TEST(Coloring, ValidatorRejectsCorruptedColorsOrderAndCount) {
    auto graph = generateGraph({4, Topology::Path, 0, 1});
    auto result = colorGraph(*graph, ColoringAlgorithm::Sequential);
    result->colors[1] = result->colors[0];
    EXPECT_FALSE(validateColoring(*graph, *result));
    result = colorGraph(*graph, ColoringAlgorithm::Sequential);
    result->processingOrder[1] = result->processingOrder[0];
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
        for (const auto& entry : coloringAlgorithms<Graph>) {
            const auto algorithm = entry.id;
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

TEST(Coloring, GreedyPassUsesProvidedOrderAndRejectsInvalidPermutations) {
    Graph graph;
    for (int i = 0; i < 4; ++i) graph.addVertex({""});
    ASSERT_TRUE(graph.addEdge(0, 2));
    ASSERT_TRUE(graph.addEdge(2, 3));
    ASSERT_TRUE(graph.addEdge(3, 1));
    auto result = greedyColorGraph(graph, DynamicArray<VertexId>{2, 3, 0, 1});
    ASSERT_TRUE(result);
    EXPECT_EQ(result->colorCount, 2);
    EXPECT_EQ(result->processingOrder[0], 2);
    EXPECT_TRUE(validateColoring(graph, *result));
    EXPECT_FALSE(greedyColorGraph(graph, DynamicArray<VertexId>{0, 1, 2}));
    EXPECT_FALSE(greedyColorGraph(graph, DynamicArray<VertexId>{0, 1, 2, 2}));
    EXPECT_FALSE(greedyColorGraph(graph, DynamicArray<VertexId>{0, 1, 2, 4}));
}

TEST(Catalogs, EntriesHaveUniqueIdsNamesAndCallableImplementations) {
    for (std::size_t i = 0; i < std::size(coloringAlgorithms<Graph>); ++i) {
        const auto& entry = coloringAlgorithms<Graph>[i];
        EXPECT_FALSE(entry.name.empty());
        ASSERT_TRUE(entry.run);
        EXPECT_EQ(algorithmName(entry.id), entry.name);
        for (std::size_t j = 0; j < i; ++j) {
            EXPECT_NE(entry.id, coloringAlgorithms<Graph>[j].id);
            EXPECT_NE(entry.name, coloringAlgorithms<Graph>[j].name);
        }
    }
    for (std::size_t i = 0; i < topologies.size(); ++i) {
        const auto& entry = topologies[i];
        EXPECT_FALSE(entry.name.empty());
        ASSERT_TRUE(entry.buildEdges);
        EXPECT_EQ(topologyName(entry.id), entry.name);
        for (std::size_t j = 0; j < i; ++j) {
            EXPECT_NE(entry.id, topologies[j].id);
            EXPECT_NE(entry.name, topologies[j].name);
        }
        auto graph = generateGraph({entry.minimumVertexCount, entry.id, 0.5, 42});
        ASSERT_TRUE(graph);
        EXPECT_EQ(graph->vertexCount(), entry.minimumVertexCount);
        auto result = colorGraph(*graph, ColoringAlgorithm::Sequential);
        ASSERT_TRUE(result);
        EXPECT_TRUE(validateColoring(*graph, *result));
        if (entry.minimumVertexCount > 0) {
            EXPECT_FALSE(generateGraph({entry.minimumVertexCount - 1, entry.id, 0.5, 42}));
        }
        auto invalidProbability = generateGraph(
            {5, entry.id, std::numeric_limits<double>::quiet_NaN(), 42});
        EXPECT_EQ(invalidProbability.has_value(), !entry.usesRandomParameters);
    }
}
