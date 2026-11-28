#include <benchmark.hpp>
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
concept AcceptedVertexData = requires { typename UnweightedUndirectedGraph<T>; };

static_assert(AcceptedVertexData<VertexData>);
static_assert(AcceptedVertexData<int>);
static_assert(AcceptedVertexData<std::string>);
static_assert(!AcceptedVertexData<ThrowingMoveConstructor>);
static_assert(!AcceptedVertexData<ThrowingMoveAssignment>);
static_assert(std::same_as<decltype(std::declval<Graph&>().addVertex(VertexData{})), Result<VertexId>>);

struct MoveOnlyVertexData {
    std::unique_ptr<int> value;

    explicit MoveOnlyVertexData(int initialValue) : value(std::make_unique<int>(initialValue)) {}
    MoveOnlyVertexData(MoveOnlyVertexData&&) noexcept = default;
    MoveOnlyVertexData& operator=(MoveOnlyVertexData&&) noexcept = default;
};

static_assert(!std::default_initializable<MoveOnlyVertexData>);
static_assert(!std::copy_constructible<MoveOnlyVertexData>);
static_assert(AcceptedVertexData<MoveOnlyVertexData>);

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

TEST(Graph, StorageGrowthDoesNotDefaultConstructUnusedVertexData) {
    UnweightedUndirectedGraph<ThrowingDefaultData> graph;
    for (int i = 0; i < 4; ++i) {
        ASSERT_TRUE(graph.addVertex(ThrowingDefaultData(i)));
    }
    ASSERT_TRUE(graph.addEdge(0, 1));
    ASSERT_TRUE(graph.addEdge(1, 3));

    ThrowingDefaultData::defaultsBeforeFailure = 0;
    EXPECT_EQ(graph.addVertex(ThrowingDefaultData(4)), 4);
    ThrowingDefaultData::defaultsBeforeFailure = -1;

    EXPECT_EQ(graph.vertexCount(), 5);
    EXPECT_EQ(graph.edgeCount(), 2);
    for (VertexId i = 0; i < 4; ++i) {
        EXPECT_EQ(graph.vertex(i)->get().data.value, static_cast<int>(i));
    }
    EXPECT_TRUE(graph.containsEdge(0, 1));
    EXPECT_TRUE(graph.containsEdge(1, 0));
    EXPECT_TRUE(graph.containsEdge(1, 3));
    EXPECT_TRUE(graph.containsEdge(3, 1));
    EXPECT_EQ(graph.addVertex(ThrowingDefaultData(5)), 5);
    EXPECT_EQ(graph.vertexCount(), 6);
    EXPECT_TRUE(graph.containsEdge(0, 1));
}

TEST(Graph, OwnsSymmetricEdgesAndRejectsInvalidChanges) {
    Graph graph;
    ASSERT_TRUE(graph.addVertex({"A"}));
    ASSERT_TRUE(graph.addVertex({"B"}));
    ASSERT_TRUE(graph.addVertex({"C"}));
    ASSERT_TRUE(graph.addEdge(0, 1));
    EXPECT_EQ(graph.edgeCount(), 1);
    EXPECT_TRUE(graph.containsEdge(1, 0));
    EXPECT_EQ(graph.neighbors(0)->get().size(), 1);
    EXPECT_EQ(graph.addEdge(1, 0).error().code, GraphErrorCode::DuplicateEdge);
    EXPECT_EQ(graph.addEdge(0, 0).error().code, GraphErrorCode::SelfLoop);
    EXPECT_EQ(graph.addEdge(0, 3).error().code, GraphErrorCode::InvalidVertex);
    EXPECT_FALSE(graph.vertex(3));
    EXPECT_FALSE(graph.neighbors(3));
    EXPECT_EQ(graph.edgeCount(), 1);
    ASSERT_TRUE(graph.removeEdge(1, 0));
    EXPECT_FALSE(graph.containsEdge(0, 1));
    EXPECT_EQ(graph.edgeCount(), 0);
    EXPECT_TRUE(graph.neighbors(0)->get().empty());
    EXPECT_EQ(graph.removeEdge(0, 1).error().code, GraphErrorCode::MissingEdge);
    EXPECT_EQ(graph.removeEdge(0, 5).error().code, GraphErrorCode::InvalidVertex);
}

TEST(Graph, SupportsArbitraryVertexDataAndMoveReuse) {
    UnweightedUndirectedGraph<int> graph;
    for (int i = 0; i < 35; ++i) {
        ASSERT_TRUE(graph.addVertex(i));
    }
    ASSERT_TRUE(graph.addEdge(1, 30));
    auto moved = std::move(graph);
    EXPECT_EQ(moved.vertex(30)->get().data, 30);
    EXPECT_EQ(moved.edgeCount(), 1);
    EXPECT_EQ(graph.vertexCount(), 0);
    EXPECT_EQ(graph.edgeCount(), 0);
    ASSERT_TRUE(graph.addVertex(42));
    EXPECT_EQ(graph.vertex(35)->get().data, 42);
    auto coloring = colorGraph(moved, ColoringAlgorithm::LargestDegreeFirst);
    ASSERT_TRUE(coloring);
    EXPECT_TRUE(validateColoring(moved, *coloring));
    graph = std::move(moved);
    ASSERT_TRUE(graph.addVertex(35));
    EXPECT_EQ(graph.vertexCount(), 36);
    EXPECT_TRUE(graph.containsEdge(30, 1));
}

TEST(Graph, RemovingMiddleNeighborPreservesOtherEdgesAndSymmetry) {
    Graph graph;
    for (int i = 0; i < 18; ++i) {
        ASSERT_TRUE(graph.addVertex({std::to_string(i)}));
    }
    for (std::size_t i = 1; i < 18; ++i) {
        ASSERT_TRUE(graph.addEdge(0, i));
    }
    ASSERT_TRUE(graph.removeEdge(0, 7));
    ASSERT_TRUE(graph.removeEdge(17, 0));
    EXPECT_EQ(graph.edgeCount(), 15);
    EXPECT_EQ(graph.neighbors(0)->get().size(), 15);
    for (std::size_t i = 1; i < 18; ++i) {
        EXPECT_EQ(graph.containsEdge(0, i), i != 7 && i != 17);
        EXPECT_EQ(graph.containsEdge(i, 0), i != 7 && i != 17);
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
    for (const auto& entry : coloringAlgorithmCatalog<Graph>) {
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
        ASSERT_TRUE(isolated.addVertex({"alone"}));
        ASSERT_TRUE(isolated.addVertex({"also alone"}));
        EXPECT_EQ(colorGraph(isolated, algorithm)->colorCount, 1);
    }
}

TEST(Coloring, DegreeOrderingAndTieBreakingAreDeterministic) {
    Graph graph;
    for (int i = 0; i < 4; ++i) {
        ASSERT_TRUE(graph.addVertex({std::to_string(i)}));
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
            ASSERT_TRUE(graph.addVertex({std::to_string(i)}));
        }
        unsigned bit = 0;
        for (std::size_t i = 0; i < 5; ++i) {
            for (std::size_t j = i + 1; j < 5; ++j, ++bit) {
                if (mask & (1u << bit)) {
                    ASSERT_TRUE(graph.addEdge(i, j));
                }
            }
        }
        for (const auto& entry : coloringAlgorithmCatalog<Graph>) {
            const auto algorithm = entry.id;
            auto result = colorGraph(graph, algorithm);
            ASSERT_TRUE(result);
            std::size_t maximumDegree = 0;
            for (std::size_t i = 0; i < 5; ++i) {
                const auto& neighbors = graph.neighbors(i)->get();
                maximumDegree = std::max(maximumDegree, neighbors.size());
                for (std::size_t j = 0; j < neighbors.size(); ++j) {
                    ASSERT_NE(result->colors[i], *result->color(neighbors[j].neighborId)) << mask;
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
            EXPECT_EQ(first->containsEdge(i, j), second->containsEdge(i, j));
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
    for (int i = 0; i < 4; ++i) ASSERT_TRUE(graph.addVertex({""}));
    ASSERT_TRUE(graph.addEdge(0, 2));
    ASSERT_TRUE(graph.addEdge(2, 3));
    ASSERT_TRUE(graph.addEdge(3, 1));
    auto result = colorGraphGreedily(graph, ArraySequence<VertexId>{2, 3, 0, 1});
    ASSERT_TRUE(result);
    EXPECT_EQ(result->colorCount, 2);
    EXPECT_EQ(result->processingOrder[0], 2);
    EXPECT_TRUE(validateColoring(graph, *result));
    EXPECT_FALSE(colorGraphGreedily(graph, ArraySequence<VertexId>{0, 1, 2}));
    EXPECT_FALSE(colorGraphGreedily(graph, ArraySequence<VertexId>{0, 1, 2, 2}));
    EXPECT_FALSE(colorGraphGreedily(graph, ArraySequence<VertexId>{0, 1, 2, 4}));
}

TEST(Catalogs, EntriesHaveUniqueIdsNamesAndCallableImplementations) {
    for (std::size_t i = 0; i < std::size(coloringAlgorithmCatalog<Graph>); ++i) {
        const auto& entry = coloringAlgorithmCatalog<Graph>[i];
        EXPECT_FALSE(entry.displayName.empty());
        ASSERT_TRUE(entry.colorGraph);
        EXPECT_EQ(coloringAlgorithmName(entry.id), entry.displayName);
        for (std::size_t j = 0; j < i; ++j) {
            EXPECT_NE(entry.id, coloringAlgorithmCatalog<Graph>[j].id);
            EXPECT_NE(entry.displayName, coloringAlgorithmCatalog<Graph>[j].displayName);
        }
    }
    for (std::size_t i = 0; i < std::size(topologyCatalog); ++i) {
        const auto& entry = topologyCatalog[i];
        EXPECT_FALSE(entry.displayName.empty());
        ASSERT_TRUE(entry.buildEdges);
        EXPECT_EQ(topologyName(entry.id), entry.displayName);
        for (std::size_t j = 0; j < i; ++j) {
            EXPECT_NE(entry.id, topologyCatalog[j].id);
            EXPECT_NE(entry.displayName, topologyCatalog[j].displayName);
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

template <class T>
concept AcceptsUnweightedEdge = requires(T& graph) { graph.addEdge(VertexId{}, VertexId{}); };
template <class T>
concept HasWeightStorageAPI = requires(const T& graph) { graph.weight(EdgeId{}); };
static_assert(!AcceptsUnweightedEdge<UndirectedGraphBase<int>>);
static_assert(AcceptsUnweightedEdge<UnweightedUndirectedGraph<int>>);
static_assert(!AcceptsUnweightedEdge<WeightedUndirectedGraph<int, std::string>>);
static_assert(!HasWeightStorageAPI<UnweightedUndirectedGraph<int>>);
static_assert(std::has_virtual_destructor_v<UndirectedGraphBase<int>>);
static_assert(!std::is_copy_constructible_v<Graph>);
static_assert(std::is_nothrow_move_constructible_v<Graph>);
static_assert(std::is_nothrow_move_assignable_v<WeightedUndirectedGraph<int, std::string>>);
static_assert(std::is_const_v<std::remove_reference_t<decltype(std::declval<const Graph&>().vertex(0)->get())>>);
static_assert(std::is_const_v<std::remove_reference_t<decltype(std::declval<const Graph&>().edge(0)->get())>>);
static_assert(std::is_const_v<std::remove_reference_t<decltype(std::declval<const Graph&>().neighbors(0)->get())>>);

template <VertexDataType T>
void expectGraphConsistency(const UndirectedGraphBase<T>& graph) {
    std::size_t adjacencyCount = 0;
    auto ids = graph.vertexIds();
    ASSERT_EQ(ids.size(), graph.vertexCount());
    for (std::size_t i = 0; i < ids.size(); ++i) {
        const auto id = ids[i];
        if (i) { EXPECT_LT(ids[i - 1], id); }
        ASSERT_TRUE(graph.vertex(id));
        EXPECT_EQ(graph.vertex(id)->get().id, id);
        const auto& list = graph.neighbors(id)->get();
        EXPECT_EQ(*graph.degree(id), list.size());
        for (const auto& adjacent : list) {
            ++adjacencyCount;
            ASSERT_TRUE(graph.containsVertex(adjacent.neighborId));
            ASSERT_TRUE(graph.edge(adjacent.edgeId));
            const auto& edge = graph.edge(adjacent.edgeId)->get();
            EXPECT_TRUE((edge.firstVertexId == id && edge.secondVertexId == adjacent.neighborId) ||
                        (edge.secondVertexId == id && edge.firstVertexId == adjacent.neighborId));
            ASSERT_TRUE(graph.edgeId(adjacent.neighborId, id));
            EXPECT_EQ(*graph.edgeId(adjacent.neighborId, id), adjacent.edgeId);
        }
    }
    EXPECT_EQ(adjacencyCount, graph.edgeCount() * 2);
    auto edges = graph.edgeIds();
    EXPECT_EQ(edges.size(), graph.edgeCount());
    for (std::size_t i = 0; i < edges.size(); ++i) {
        if (i) { EXPECT_LT(edges[i - 1], edges[i]); }
        const auto& edge = graph.edge(edges[i])->get();
        EXPECT_EQ(edge.id, edges[i]);
        EXPECT_EQ(*graph.edgeId(edge.firstVertexId, edge.secondVertexId), edge.id);
    }
}

TEST(Graph, IsolatedAndIncidentVertexRemovalPreservesIdentifiersAndIndexes) {
    UnweightedUndirectedGraph<int> graph;
    for (int i = 0; i < 7; ++i) ASSERT_TRUE(graph.addVertex(i * 10));
    const auto first = *graph.addEdge(0, 2);
    const auto removed = *graph.addEdge(2, 4);
    const auto last = *graph.addEdge(4, 6);
    ASSERT_TRUE(graph.removeVertex(1));
    EXPECT_EQ(graph.vertex(6)->get().data, 60);
    ASSERT_TRUE(graph.removeEdge(removed));
    EXPECT_EQ(graph.edge(last)->get().id, last);
    EXPECT_EQ(*graph.edgeId(6, 4), last);
    expectGraphConsistency<int>(graph);
    ASSERT_TRUE(graph.removeVertex(2));
    EXPECT_FALSE(graph.containsEdge(first));
    EXPECT_FALSE(graph.containsVertex(2));
    EXPECT_EQ(graph.vertex(6)->get().data, 60);
    EXPECT_EQ(graph.vertex(4)->get().data, 40);
    EXPECT_EQ(graph.edgeCount(), 1);
    EXPECT_EQ(graph.addVertex(70), 7);
    const auto added = *graph.addEdge(0, 7);
    EXPECT_GT(added, last);
    expectGraphConsistency<int>(graph);
    EXPECT_FALSE(graph.removeVertex(1));
    EXPECT_FALSE(graph.removeEdge(removed));
    EXPECT_FALSE(graph.addVertexWithId(1, 1));
    EXPECT_FALSE(graph.addEdgeWithId(removed, 0, 4));
    expectGraphConsistency<int>(graph);
}

TEST(Graph, RepeatedDenseCompactionKeepsEveryIdentifierResolvable) {
    UnweightedUndirectedGraph<int> graph;
    for (int i = 0; i < 32; ++i) ASSERT_TRUE(graph.addVertex(i));
    for (VertexId i = 0; i < 32; ++i)
        for (VertexId j = i + 1; j < 32; ++j)
            if ((i + j) % 3 == 0) { ASSERT_TRUE(graph.addEdge(i, j)); }
    for (VertexId i = 1; i < 32; i += 2) {
        ASSERT_TRUE(graph.removeVertex(i));
        expectGraphConsistency<int>(graph);
        for (const auto id : graph.vertexIds()) EXPECT_EQ(graph.vertex(id)->get().data, static_cast<int>(id));
    }
    const auto edges = graph.edgeIds();
    for (std::size_t i = 0; i < edges.size(); i += 2) {
        ASSERT_TRUE(graph.removeEdge(edges[i]));
        expectGraphConsistency<int>(graph);
    }
}

TEST(Graph, ClearAndMoveKeepHighWaterMarksAndAllowReuse) {
    Graph source;
    for (int i = 0; i < 4; ++i) ASSERT_TRUE(source.addVertex({""}));
    const auto edge = *source.addEdge(0, 3);
    ASSERT_TRUE(source.removeVertex(1));
    Graph moved(std::move(source));
    EXPECT_EQ(source.vertexCount(), 0);
    EXPECT_EQ(source.edgeCount(), 0);
    EXPECT_EQ(source.addVertex({"reuse"}), 4);
    source.clear();
    EXPECT_EQ(source.addVertex({"again"}), 5);
    EXPECT_EQ(source.addVertex({"another"}), 6);
    EXPECT_GT(*source.addEdge(5, 6), edge);
    moved = std::move(source);
    EXPECT_TRUE(moved.containsVertex(5));
    EXPECT_EQ(moved.addVertex({"next"}), 7);
    EXPECT_EQ(source.addVertex({"source"}), 7);
    auto& alias = moved;
    moved = std::move(alias);
    expectGraphConsistency<VertexData>(moved);
    moved.clear();
    EXPECT_EQ(moved.addVertex({"after clear"}), 8);
}

TEST(Graph, ExhaustedCountersNeverWrapAcrossClearOrMove) {
    constexpr auto maximum = std::numeric_limits<std::size_t>::max();
    UnweightedUndirectedGraph<int> graph;
    ASSERT_TRUE(graph.addVertexWithId(maximum - 1, 1));
    EXPECT_EQ(graph.addVertex(2), maximum);
    auto rejected = graph.addVertex(3);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code, GraphErrorCode::IdentifierExhausted);
    ASSERT_TRUE(graph.advanceIdCounters(graph.vertexIdCounter(), {maximum, false}));
    auto edge = graph.addEdge(maximum - 1, maximum);
    ASSERT_TRUE(edge);
    EXPECT_EQ(*edge, maximum);
    ASSERT_TRUE(graph.removeEdge(*edge));
    EXPECT_EQ(graph.addEdge(maximum - 1, maximum).error().code, GraphErrorCode::IdentifierExhausted);
    EXPECT_EQ(graph.edgeCount(), 0);
    auto moved = std::move(graph);
    rejected = graph.addVertex(4);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code, GraphErrorCode::IdentifierExhausted);
    moved.clear();
    rejected = moved.addVertex(4);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code, GraphErrorCode::IdentifierExhausted);
    EXPECT_FALSE(moved.advanceIdCounters({0, false}, {0, false}));
    EXPECT_TRUE(moved.vertexIdCounter().exhausted);
    EXPECT_TRUE(moved.edgeIdCounter().exhausted);
}

struct NonNumericWeight {
    std::unique_ptr<std::string> text;
    explicit NonNumericWeight(std::string value) : text(std::make_unique<std::string>(std::move(value))) {}
    NonNumericWeight(NonNumericWeight&&) noexcept = default;
    NonNumericWeight& operator=(NonNumericWeight&&) noexcept = default;
};

TEST(WeightedGraph, StoresMoveOnlyNonDefaultNonNumericWeightsAndColorsThroughBase) {
    WeightedUndirectedGraph<int, NonNumericWeight> graph;
    ASSERT_TRUE(graph.addVertex(10));
    ASSERT_TRUE(graph.addVertex(20));
    const auto edge = graph.addEdge(0, 1, NonNumericWeight("initial"));
    ASSERT_TRUE(edge);
    EXPECT_EQ(*graph.weight(*edge)->get().text, "initial");
    ASSERT_TRUE(graph.setWeight(*edge, NonNumericWeight("updated")));
    EXPECT_EQ(*graph.weight(*edge)->get().text, "updated");
    const UndirectedGraphBase<int>& base = graph;
    auto result = colorGraph(base, ColoringAlgorithm::LargestDegreeFirst);
    ASSERT_TRUE(result);
    EXPECT_TRUE(validateColoring(base, *result));
    EXPECT_EQ(result->colorCount, 2);
    EXPECT_FALSE(graph.setWeight(*edge + 1, NonNumericWeight("missing")));
    EXPECT_FALSE(graph.weight(*edge + 1));
    EXPECT_FALSE(graph.addEdge(1, 0, NonNumericWeight("duplicate")));
    EXPECT_FALSE(graph.addEdge(0, 0, NonNumericWeight("loop")));
    EXPECT_FALSE(graph.addEdge(0, 3, NonNumericWeight("invalid")));
    EXPECT_EQ(*graph.weight(*edge)->get().text, "updated");
    EXPECT_EQ(graph.edgeCount(), 1);
    expectGraphConsistency<int>(graph);
}

TEST(WeightedGraph, RemovesWeightsViaBaseForEdgesVerticesAndClear) {
    WeightedUndirectedGraph<int, std::string> graph;
    for (int i = 0; i < 5; ++i) ASSERT_TRUE(graph.addVertex(i));
    const auto a = *graph.addEdge(0, 1, "a");
    const auto b = *graph.addEdge(1, 2, "b");
    const auto c = *graph.addEdge(2, 3, "c");
    const auto d = *graph.addEdge(3, 4, "d");
    UndirectedGraphBase<int>& base = graph;
    ASSERT_TRUE(base.removeEdge(a));
    EXPECT_FALSE(graph.weight(a));
    EXPECT_EQ(graph.weight(d)->get(), "d");
    ASSERT_TRUE(base.removeVertex(2));
    EXPECT_FALSE(graph.weight(b));
    EXPECT_FALSE(graph.weight(c));
    EXPECT_EQ(graph.weight(d)->get(), "d");
    ASSERT_TRUE(base.removeEdge(4, 3));
    EXPECT_FALSE(graph.weight(d));
    const auto e = *graph.addEdge(0, 4, "e");
    base.clear();
    EXPECT_FALSE(graph.weight(e));
    EXPECT_EQ(base.vertexCount(), 0);
    EXPECT_EQ(graph.addVertex(5), 5);
    EXPECT_EQ(graph.addVertex(6), 6);
    EXPECT_GT(*graph.addEdge(5, 6, "new"), e);
    expectGraphConsistency<int>(graph);
}

struct ObservedWeight {
    std::shared_ptr<int> lifetime;
    explicit ObservedWeight(std::shared_ptr<int> token) : lifetime(std::move(token)) {}
};

TEST(WeightedGraph, DeletionClearAndPolymorphicDestructionReleaseActualWeights) {
    auto token = std::make_shared<int>(0);
    auto graph = std::make_unique<WeightedUndirectedGraph<int, ObservedWeight>>();
    for (int i = 0; i < 3; ++i) ASSERT_TRUE(graph->addVertex(i));
    const auto a = *graph->addEdge(0, 1, ObservedWeight(token));
    const auto b = *graph->addEdge(1, 2, ObservedWeight(token));
    EXPECT_EQ(token.use_count(), 3);
    ASSERT_TRUE(graph->removeEdge(a));
    EXPECT_EQ(token.use_count(), 2);
    ASSERT_TRUE(graph->removeEdge(b));
    EXPECT_EQ(token.use_count(), 1);
    ASSERT_TRUE(graph->addEdge(0, 2, ObservedWeight(token)));
    graph->clear();
    EXPECT_EQ(token.use_count(), 1);
    ASSERT_TRUE(graph->addVertex(3));
    ASSERT_TRUE(graph->addVertex(4));
    ASSERT_TRUE(graph->addEdge(3, 4, ObservedWeight(token)));
    std::unique_ptr<UndirectedGraphBase<int>> base = std::move(graph);
    base.reset();
    EXPECT_EQ(token.use_count(), 1);
}

TEST(WeightedGraph, MoveConstructionAssignmentAndSourceReusePreserveWeights) {
    WeightedUndirectedGraph<int, std::string> source;
    ASSERT_TRUE(source.addVertex(0));
    ASSERT_TRUE(source.addVertex(1));
    auto edge = *source.addEdge(0, 1, "original");
    auto moved = std::move(source);
    EXPECT_EQ(moved.weight(edge)->get(), "original");
    EXPECT_FALSE(source.weight(edge));
    EXPECT_EQ(source.addVertex(2), 2);
    ASSERT_TRUE(source.addVertex(3));
    EXPECT_GT(*source.addEdge(2, 3, "reused"), edge);
    moved = std::move(source);
    EXPECT_EQ(moved.weight(1)->get(), "reused");
    EXPECT_FALSE(moved.weight(edge));
    EXPECT_FALSE(source.weight(1));
    EXPECT_EQ(source.vertexCount(), 0);
    EXPECT_EQ(source.addVertex(4), 4);
    expectGraphConsistency<int>(moved);
}

TEST(Coloring, GappedIdentifiersAndTieBreakingAfterCompaction) {
    Graph graph;
    for (int i = 0; i < 8; ++i) ASSERT_TRUE(graph.addVertex({std::to_string(i)}));
    ASSERT_TRUE(graph.removeVertex(1));
    ASSERT_TRUE(graph.removeVertex(3));
    ASSERT_TRUE(graph.removeVertex(5));
    ASSERT_TRUE(graph.addEdge(0, 2));
    ASSERT_TRUE(graph.addEdge(2, 4));
    ASSERT_TRUE(graph.addEdge(4, 6));
    for (const auto& algorithm : coloringAlgorithmCatalog<UndirectedGraphBase<VertexData>>) {
        const UndirectedGraphBase<VertexData>& base = graph;
        auto result = algorithm.colorGraph(base);
        ASSERT_TRUE(result);
        EXPECT_TRUE(validateColoring(base, *result));
        EXPECT_FALSE(result->color(1));
        EXPECT_NE(*result->color(0), *result->color(2));
        EXPECT_NE(*result->color(2), *result->color(4));
        auto corrupted = *result;
        corrupted.vertexIds[0] = 1;
        EXPECT_FALSE(validateColoring(base, corrupted));
        corrupted = *result;
        corrupted.processingOrder[0] = 1;
        EXPECT_FALSE(validateColoring(base, corrupted));
        corrupted = *result;
        corrupted.processingOrder[0] = corrupted.processingOrder[1];
        EXPECT_FALSE(validateColoring(base, corrupted));
    }
    auto sequential = colorGraph(graph, ColoringAlgorithm::Sequential);
    EXPECT_EQ(sequential->processingOrder[0], 0);
    EXPECT_EQ(sequential->processingOrder[1], 2);
    EXPECT_EQ(sequential->processingOrder[4], 7);
    auto degree = colorGraph(graph, ColoringAlgorithm::LargestDegreeFirst);
    EXPECT_EQ(degree->processingOrder[0], 2);
    EXPECT_EQ(degree->processingOrder[1], 4);
    EXPECT_EQ(degree->processingOrder[2], 0);
    EXPECT_EQ(degree->processingOrder[3], 6);
    EXPECT_EQ(degree->processingOrder[4], 7);
    auto comparison = compareAlgorithms(graph, 1);
    ASSERT_TRUE(comparison);
    EXPECT_EQ((*comparison)[0].vertexCount, 5);
}

TEST(Generator, TopologyCallbacksUseExistingIdsWithGaps) {
    for (const auto& entry : topologyCatalog) {
        Graph graph;
        for (int i = 0; i < 8; ++i) ASSERT_TRUE(graph.addVertex({""}));
        ASSERT_TRUE(graph.removeVertex(1));
        ASSERT_TRUE(graph.removeVertex(3));
        ASSERT_TRUE(graph.removeVertex(5));
        ASSERT_TRUE(entry.buildEdges(graph, {5, entry.id, 1, 1}));
        expectGraphConsistency<VertexData>(graph);
        auto result = colorGraph(graph, ColoringAlgorithm::Sequential);
        ASSERT_TRUE(result);
        EXPECT_TRUE(validateColoring(graph, *result));
    }
}

TEST(Generator, TopologyCallbacksRejectMismatchedCountsBeforeAddingEdges) {
    for (const auto& entry : topologyCatalog) {
        Graph graph;
        for (int i = 0; i < 5; ++i) ASSERT_TRUE(graph.addVertex({""}));
        ASSERT_TRUE(graph.removeVertex(1));
        ASSERT_TRUE(graph.removeVertex(3));
        const auto nextEdgeId = graph.edgeIdCounter().nextId;

        for (const std::size_t count : {2u, 4u}) {
            auto result = entry.buildEdges(graph, {count, entry.id, 1, 1});
            ASSERT_FALSE(result) << entry.displayName;
            EXPECT_EQ(result.error().code, GraphErrorCode::InvalidParameters);
            EXPECT_EQ(graph.vertexCount(), 3);
            EXPECT_EQ(graph.edgeCount(), 0);
            EXPECT_EQ(graph.edgeIdCounter().nextId, nextEdgeId);
            expectGraphConsistency<VertexData>(graph);
        }
    }
}

TEST(GraphDictionaryBoundary, ConvertsDependencyErrorsAndHonorsMissingKeys) {
    SortedArrayDictionary<std::size_t, std::size_t> storage;
    IDictionary<std::size_t, std::size_t>& dictionary = storage;
    ASSERT_TRUE(addGraphDictionaryEntry(dictionary, 7, std::size_t{12}));
    auto duplicate = addGraphDictionaryEntry(dictionary, 7, std::size_t{99});
    ASSERT_FALSE(duplicate);
    EXPECT_EQ(duplicate.error().code, GraphErrorCode::Unexpected);
    EXPECT_FALSE(duplicate.error().detail.empty());
    ASSERT_TRUE(dictionary.find(7));
    EXPECT_EQ(*dictionary.find(7), 12);
    EXPECT_FALSE(updateGraphDictionaryEntry(dictionary, 9, std::size_t{33}));
    EXPECT_FALSE(dictionary.find(9));
    EXPECT_FALSE(dictionary.remove(9));
    EXPECT_EQ(dictionary.getCount(), 1);
}

TEST(WeightedGraph, ExhaustionAndMissingElementFailuresPreserveWeightsAndCounters) {
    WeightedUndirectedGraph<int, std::string> graph;
    ASSERT_TRUE(graph.addVertex(0));
    ASSERT_TRUE(graph.addVertex(1));
    constexpr auto maximum = std::numeric_limits<std::size_t>::max();
    const auto before = graph.vertexIdCounter();
    EXPECT_FALSE(graph.advanceIdCounters({100, false}, {2, true}));
    EXPECT_EQ(graph.vertexIdCounter().nextId, before.nextId);
    ASSERT_TRUE(graph.advanceIdCounters(before, {maximum, false}));
    const auto edge = graph.addEdge(0, 1, "last");
    ASSERT_TRUE(edge);
    EXPECT_EQ(*edge, maximum);
    EXPECT_FALSE(graph.removeVertex(99));
    EXPECT_FALSE(graph.removeEdge(maximum - 1));
    EXPECT_FALSE(graph.setWeight(maximum - 1, "missing"));
    EXPECT_FALSE(graph.addEdge(0, 0, "loop"));
    EXPECT_EQ(graph.weight(maximum)->get(), "last");
    ASSERT_TRUE(graph.removeEdge(maximum));
    auto exhausted = graph.addEdge(0, 1, "cannot insert");
    ASSERT_FALSE(exhausted);
    EXPECT_EQ(exhausted.error().code, GraphErrorCode::IdentifierExhausted);
    EXPECT_FALSE(graph.weight(maximum));
    EXPECT_EQ(graph.edgeCount(), 0);
    EXPECT_TRUE(graph.edgeIdCounter().exhausted);
    expectGraphConsistency<int>(graph);
}

TEST(GraphContainerBoundary, ConvertsContainerErrorsToGraphErrors) {
    auto capacity = failure(ContainerError{ContainerErrorCode::CapacityExceeded, 100, 50});
    EXPECT_EQ(capacity.error().code, GraphErrorCode::LimitExceeded);
    EXPECT_NE(capacity.error().detail.find("100"), std::string::npos);
    auto index = failure(ContainerError{ContainerErrorCode::IndexOutOfRange, 3, 2});
    EXPECT_EQ(index.error().code, GraphErrorCode::Unexpected);
}
