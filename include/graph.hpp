#pragma once

#include "errors.hpp"
#include <concepts>
#include "sequence/array_sequence.hpp"
#include <dictionary/sorted_array_dictionary.hpp>
#include <cstddef>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

using VertexId = std::size_t;
using EdgeId = std::size_t;

struct VertexData {
    std::string label;
};

template <class TData>
concept VertexDataType = std::movable<TData> &&
                         std::is_nothrow_move_constructible_v<TData> &&
                         std::is_nothrow_move_assignable_v<TData>;

template <class TData>
struct GraphVertex {
    VertexId id{};
    TData data{};
};

struct GraphEdge {
    EdgeId id{};
    VertexId firstVertexId{}, secondVertexId{};
};

struct AdjacencyEntry {
    VertexId neighborId{};
    EdgeId edgeId{};
};

struct GraphIdCounter {
    std::size_t nextId = 0;
    bool exhausted = false;

    bool accepts(std::size_t id) const noexcept { return !exhausted && id >= nextId; }
    void consume(std::size_t id) noexcept {
        exhausted = id == std::numeric_limits<std::size_t>::max();
        nextId = exhausted ? id : id + 1;
    }
    void merge(const GraphIdCounter& other) noexcept {
        if (other.nextId > nextId) {
            nextId = other.nextId;
        }
        exhausted = exhausted || other.exhausted;
    }
};

template <class TValue>
Result<void> addGraphDictionaryEntry(IDictionary<std::size_t, TValue>& dictionary,
                                     std::size_t id, const TValue& value) {
    auto result = dictionary.add(id, value);
    if (!result) {
        return failure(GraphErrorCode::Unexpected, formatError(result.error()));
    }
    return {};
}

// std::function для reference_wrapper не выделяет память. Это важно в удалении:
// modify копирует только индекс или shared_ptr, затем заменяет эквивалентную запись
template <class TValue>
bool updateGraphDictionaryEntry(IDictionary<std::size_t, TValue>& dictionary,
                                 std::size_t id, const TValue& value) {
    auto replace = [&value](TValue& current) { current = value; };
    return dictionary.modify(id, std::ref(replace));
}

template <VertexDataType TVertexData>
class UndirectedGraphBase {
    ArraySequence<GraphVertex<TVertexData>> vertices_;
    ArraySequence<GraphEdge> edges_;
    ArraySequence<ArraySequence<AdjacencyEntry>> adjacency_;
    SortedArrayDictionary<VertexId, std::size_t> vertexPositions_;
    SortedArrayDictionary<EdgeId, std::size_t> edgePositions_;
    GraphIdCounter vertexCounter_, edgeCounter_;

    IDictionary<VertexId, std::size_t>& vertexIndex() noexcept { return vertexPositions_; }
    const IDictionary<VertexId, std::size_t>& vertexIndex() const noexcept { return vertexPositions_; }
    IDictionary<EdgeId, std::size_t>& edgeIndex() noexcept { return edgePositions_; }
    const IDictionary<EdgeId, std::size_t>& edgeIndex() const noexcept { return edgePositions_; }

    static void eraseNeighbor(ArraySequence<AdjacencyEntry>& entries, EdgeId id) noexcept {
        for (std::size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].edgeId == id) {
                entries[i] = entries[entries.size() - 1];
                entries.pop_back();
                return;
            }
        }
    }

    Result<VertexId> insertVertex(VertexId id, TVertexData data) {
        if (vertexCounter_.exhausted) {
            return failure(GraphErrorCode::IdentifierExhausted);
        }
        if (!vertexCounter_.accepts(id)) {
            return failure(GraphErrorCode::InvalidParameters, "Vertex ID has already been issued or skipped.");
        }
        auto verticesReserved = vertices_.reserve(vertices_.size() + 1);
        if (!verticesReserved) return failure(verticesReserved.error());
        auto adjacencyReserved = adjacency_.reserve(adjacency_.size() + 1);
        if (!adjacencyReserved) return failure(adjacencyReserved.error());
        auto indexed = addGraphDictionaryEntry(vertexIndex(), id, vertices_.size());
        if (!indexed) {
            return std::unexpected(indexed.error());
        }
        vertices_.push_back({id, std::move(data)}).value();
        adjacency_.push_back({}).value();
        vertexCounter_.consume(id);
        return id;
    }

protected:
    UndirectedGraphBase() = default;
    UndirectedGraphBase(const UndirectedGraphBase&) = delete;
    UndirectedGraphBase& operator=(const UndirectedGraphBase&) = delete;
    UndirectedGraphBase(UndirectedGraphBase&& other) noexcept
        : vertices_(std::move(other.vertices_)), edges_(std::move(other.edges_)),
          adjacency_(std::move(other.adjacency_)), vertexPositions_(std::move(other.vertexPositions_)),
          edgePositions_(std::move(other.edgePositions_)), vertexCounter_(other.vertexCounter_),
          edgeCounter_(other.edgeCounter_) {}

    UndirectedGraphBase& operator=(UndirectedGraphBase&& other) noexcept {
        if (this != &other) {
            vertices_ = std::move(other.vertices_);
            edges_ = std::move(other.edges_);
            adjacency_ = std::move(other.adjacency_);
            vertexPositions_ = std::move(other.vertexPositions_);
            edgePositions_ = std::move(other.edgePositions_);
            vertexCounter_.merge(other.vertexCounter_);
            edgeCounter_.merge(other.edgeCounter_);
        }
        return *this;
    }

    virtual void eraseEdgeData(EdgeId) noexcept {}
    virtual void clearEdgeData() noexcept {}

    struct PreparedEdgeInsertion {
        EdgeId id;
        std::size_t firstPosition, secondPosition;
    };

    Result<PreparedEdgeInsertion> prepareEdgeInsertion(VertexId first, VertexId second, EdgeId id) {
        const auto firstPosition = vertexIndex().find(first);
        const auto secondPosition = vertexIndex().find(second);
        if (!firstPosition || !secondPosition) {
            return failure(GraphErrorCode::InvalidVertex);
        }
        if (first == second) {
            return failure(GraphErrorCode::SelfLoop);
        }
        auto& left = adjacency_[*firstPosition];
        auto& right = adjacency_[*secondPosition];
        for (const auto& entry : left) {
            if (entry.neighborId == second) {
                return failure(GraphErrorCode::DuplicateEdge);
            }
        }
        if (edgeCounter_.exhausted) {
            return failure(GraphErrorCode::IdentifierExhausted);
        }
        if (!edgeCounter_.accepts(id)) {
            return failure(GraphErrorCode::InvalidParameters, "Edge ID has already been issued or skipped.");
        }
        auto edgesReserved = edges_.reserve(edges_.size() + 1);
        if (!edgesReserved) return failure(edgesReserved.error());
        auto leftReserved = left.reserve(left.size() + 1);
        if (!leftReserved) return failure(leftReserved.error());
        auto rightReserved = right.reserve(right.size() + 1);
        if (!rightReserved) return failure(rightReserved.error());
        return PreparedEdgeInsertion{id, *firstPosition, *secondPosition};
    }

    EdgeId nextEdgeId() const noexcept { return edgeCounter_.nextId; }

    // Подготовка уже проверила концы и зарезервировала место для всех 3-х записей
    Result<EdgeId> insertEdge(const PreparedEdgeInsertion& insertion) {
        const auto first = vertices_[insertion.firstPosition].id;
        const auto second = vertices_[insertion.secondPosition].id;
        auto indexed = addGraphDictionaryEntry(edgeIndex(), insertion.id, edges_.size());
        if (!indexed) {
            return std::unexpected(indexed.error());
        }
        edges_.push_back({insertion.id, first, second}).value();
        adjacency_[insertion.firstPosition].push_back({second, insertion.id}).value();
        adjacency_[insertion.secondPosition].push_back({first, insertion.id}).value();
        edgeCounter_.consume(insertion.id);
        return insertion.id;
    }

public:
    virtual ~UndirectedGraphBase() = default;

    std::size_t vertexCount() const noexcept { return vertices_.size(); }
    std::size_t edgeCount() const noexcept { return edges_.size(); }
    bool containsVertex(VertexId id) const { return vertexIndex().containsKey(id); }
    bool containsEdge(EdgeId id) const { return edgeIndex().containsKey(id); }

    Result<VertexId> addVertex(TVertexData data) {
        return insertVertex(vertexCounter_.nextId, std::move(data));
    }

    Result<VertexId> addVertexWithId(VertexId id, TVertexData data) {
        return insertVertex(id, std::move(data));
    }

    Result<void> setVertexData(VertexId id, TVertexData data) {
        const auto position = vertexIndex().find(id);
        if (!position) {
            return failure(GraphErrorCode::InvalidVertex);
        }
        vertices_[*position].data = std::move(data);
        return {};
    }

    Result<std::reference_wrapper<const GraphVertex<TVertexData>>> vertex(VertexId id) const {
        auto position = vertexIndex().find(id);
        if (!position) {
            return failure(GraphErrorCode::InvalidVertex);
        }
        return std::cref(vertices_[*position]);
    }
    Result<std::reference_wrapper<const GraphEdge>> edge(EdgeId id) const {
        auto position = edgeIndex().find(id);
        if (!position) {
            return failure(GraphErrorCode::MissingEdge);
        }
        return std::cref(edges_[*position]);
    }
    Result<std::reference_wrapper<const ArraySequence<AdjacencyEntry>>> neighbors(VertexId id) const {
        auto position = vertexIndex().find(id);
        if (!position) {
            return failure(GraphErrorCode::InvalidVertex);
        }
        return std::cref(adjacency_[*position]);
    }
    Result<std::size_t> degree(VertexId id) const {
        auto list = neighbors(id);
        if (!list) {
            return std::unexpected(list.error());
        }
        return list->get().size();
    }
    ArraySequence<VertexId> vertexIds() const {
        const auto keys = vertexIndex().getKeys();
        ArraySequence<VertexId> result;
        result.reserve(keys.size()).value();
        for (const auto key : keys) result.push_back(key).value();
        return result;
    }
    ArraySequence<EdgeId> edgeIds() const {
        const auto keys = edgeIndex().getKeys();
        ArraySequence<EdgeId> result;
        result.reserve(keys.size()).value();
        for (const auto key : keys) result.push_back(key).value();
        return result;
    }

    Result<EdgeId> edgeId(VertexId first, VertexId second) const {
        const auto firstPosition = vertexIndex().find(first);
        if (!firstPosition || !vertexIndex().containsKey(second)) {
            return failure(GraphErrorCode::InvalidVertex);
        }
        const auto& entries = adjacency_[*firstPosition];
        for (const auto& entry : entries) {
            if (entry.neighborId == second) {
                return entry.edgeId;
            }
        }
        return failure(GraphErrorCode::MissingEdge);
    }
    bool containsEdge(VertexId first, VertexId second) const { return edgeId(first, second).has_value(); }

    Result<void> removeEdge(EdgeId id) {
        auto position = edgeIndex().find(id);
        if (!position) {
            return failure(GraphErrorCode::MissingEdge);
        }
        const auto removed = edges_[*position];
        eraseEdgeData(id);
        eraseNeighbor(adjacency_[*vertexIndex().find(removed.firstVertexId)], id);
        eraseNeighbor(adjacency_[*vertexIndex().find(removed.secondVertexId)], id);
        const auto last = edges_.size() - 1;
        if (*position != last) {
            edges_[*position] = edges_[last];
            updateGraphDictionaryEntry(edgeIndex(), edges_[*position].id, *position);
        }
        edges_.pop_back();
        edgeIndex().remove(id);
        return {};
    }
    Result<void> removeEdge(VertexId first, VertexId second) {
        auto id = edgeId(first, second);
        if (!id) {
            return std::unexpected(id.error());
        }
        return removeEdge(*id);
    }
    Result<void> removeVertex(VertexId id) {
        auto position = vertexIndex().find(id);
        if (!position) {
            return failure(GraphErrorCode::InvalidVertex);
        }
        auto& entries = adjacency_[*position];
        while (!entries.empty()) {
            (void)removeEdge(entries[entries.size() - 1].edgeId);
        }
        const auto last = vertices_.size() - 1;
        if (*position != last) {
            vertices_[*position] = std::move(vertices_[last]);
            adjacency_[*position] = std::move(adjacency_[last]);
            updateGraphDictionaryEntry(vertexIndex(), vertices_[*position].id, *position);
        }
        vertices_.pop_back();
        adjacency_.pop_back();
        vertexIndex().remove(id);
        return {};
    }
    void clear() noexcept {
        clearEdgeData();
        vertices_ = {};
        edges_ = {};
        adjacency_ = {};
        vertexPositions_ = {};
        edgePositions_ = {};
    }

    GraphIdCounter vertexIdCounter() const noexcept { return vertexCounter_; }
    GraphIdCounter edgeIdCounter() const noexcept { return edgeCounter_; }
    Result<void> advanceIdCounters(GraphIdCounter vertices, GraphIdCounter edges) {
        const auto valid = [](GraphIdCounter current, GraphIdCounter proposed) {
            return proposed.nextId >= current.nextId && (!current.exhausted || proposed.exhausted) &&
                   (!proposed.exhausted || proposed.nextId == std::numeric_limits<std::size_t>::max());
        };
        if (!valid(vertexCounter_, vertices) || !valid(edgeCounter_, edges)) {
            return failure(GraphErrorCode::InvalidParameters, "Invalid identifier high-water marks.");
        }
        vertexCounter_ = vertices;
        edgeCounter_ = edges;
        return {};
    }
};

template <VertexDataType TVertexData>
class UnweightedUndirectedGraph final : public UndirectedGraphBase<TVertexData> {
public:
    UnweightedUndirectedGraph() = default;
    UnweightedUndirectedGraph(UnweightedUndirectedGraph&&) noexcept = default;
    UnweightedUndirectedGraph& operator=(UnweightedUndirectedGraph&&) noexcept = default;

    Result<EdgeId> addEdge(VertexId first, VertexId second) {
        return addEdgeWithId(this->nextEdgeId(), first, second);
    }
    Result<EdgeId> addEdgeWithId(EdgeId id, VertexId first, VertexId second) {
        auto insertion = this->prepareEdgeInsertion(first, second, id);
        if (!insertion) {
            return std::unexpected(insertion.error());
        }
        return this->insertEdge(*insertion);
    }
};

template <VertexDataType TVertexData, class TWeight>
requires std::move_constructible<TWeight>
class WeightedUndirectedGraph final : public UndirectedGraphBase<TVertexData> {
    using WeightHandle = std::shared_ptr<const TWeight>;
    SortedArrayDictionary<EdgeId, WeightHandle> weights_;
    IDictionary<EdgeId, WeightHandle>& weights() noexcept { return weights_; }
    const IDictionary<EdgeId, WeightHandle>& weights() const noexcept { return weights_; }

    void eraseEdgeData(EdgeId id) noexcept override {
        updateGraphDictionaryEntry(weights(), id, WeightHandle{});
        weights().remove(id);
    }
    void clearEdgeData() noexcept override { weights_ = {}; }
public:
    WeightedUndirectedGraph() = default;
    WeightedUndirectedGraph(WeightedUndirectedGraph&&) noexcept = default;
    WeightedUndirectedGraph& operator=(WeightedUndirectedGraph&&) noexcept = default;

    Result<EdgeId> addEdge(VertexId first, VertexId second, TWeight weight) {
        const auto id = this->nextEdgeId();
        auto insertion = this->prepareEdgeInsertion(first, second, id);
        if (!insertion) {
            return std::unexpected(insertion.error());
        }
        auto added = addGraphDictionaryEntry(weights(), id, std::make_shared<const TWeight>(std::move(weight)));
        if (!added) {
            return std::unexpected(added.error());
        }
        try {
            auto edge = this->insertEdge(*insertion);
            if (!edge) {
                eraseEdgeData(id);
            }
            return edge;
        } catch (...) {
            eraseEdgeData(id);
            throw;
        }
    }
    Result<std::reference_wrapper<const TWeight>> weight(EdgeId id) const {
        auto handle = weights().find(id);
        if (!handle) {
            return failure(GraphErrorCode::MissingEdge);
        }
        return std::cref(**handle);
    }
    Result<void> setWeight(EdgeId id, TWeight weight) {
        if (!this->containsEdge(id)) {
            return failure(GraphErrorCode::MissingEdge);
        }
        auto replacement = std::make_shared<const TWeight>(std::move(weight));
        if (!updateGraphDictionaryEntry(weights(), id, replacement)) {
            return failure(GraphErrorCode::Unexpected, "Missing weight for an existing edge.");
        }
        return {};
    }
};

using Graph = UnweightedUndirectedGraph<VertexData>;
