#pragma once

#include "errors.hpp"
#include <concepts>
#include <containers/dynamic_array.hpp>
#include <dictionary/sorted_array_dictionary.hpp>
#include <cstddef>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

using VertexId = std::size_t;
using EdgeId = std::size_t;

struct VertexData {
    std::string label;
};

template <class TData>
concept GraphData = std::default_initializable<TData> && std::movable<TData> &&
                    std::is_nothrow_move_constructible_v<TData> &&
                    std::is_nothrow_move_assignable_v<TData>;

template <class TData>
struct GraphVertex {
    VertexId id{};
    TData data{};
};

struct GraphEdge {
    EdgeId id{};
    VertexId first{}, second{};
};

struct AdjacencyEntry {
    VertexId neighbor{};
    EdgeId edge{};
};

struct GraphIdCounter {
    std::size_t next = 0;
    bool exhausted = false;

    bool accepts(std::size_t id) const noexcept { return !exhausted && id >= next; }
    void consume(std::size_t id) noexcept {
        exhausted = id == std::numeric_limits<std::size_t>::max();
        next = exhausted ? id : id + 1;
    }
    void merge(const GraphIdCounter& other) noexcept {
        if (other.next > next) {
            next = other.next;
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

template <GraphData TVertexData>
class UndirectedGraphBase {
    DynamicArray<GraphVertex<TVertexData>> vertices_;
    DynamicArray<GraphEdge> edges_;
    DynamicArray<DynamicArray<AdjacencyEntry>> adjacency_;
    SortedArrayDictionary<VertexId, std::size_t> vertexPositions_;
    SortedArrayDictionary<EdgeId, std::size_t> edgePositions_;
    GraphIdCounter vertexCounter_, edgeCounter_;

    IDictionary<VertexId, std::size_t>& vertexIndex() noexcept { return vertexPositions_; }
    const IDictionary<VertexId, std::size_t>& vertexIndex() const noexcept { return vertexPositions_; }
    IDictionary<EdgeId, std::size_t>& edgeIndex() noexcept { return edgePositions_; }
    const IDictionary<EdgeId, std::size_t>& edgeIndex() const noexcept { return edgePositions_; }

    static void eraseNeighbor(DynamicArray<AdjacencyEntry>& entries, EdgeId id) noexcept {
        for (std::size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].edge == id) {
                entries[i] = entries[entries.size() - 1];
                entries.pop_back();
                return;
            }
        }
    }

    template <class T>
    static void reserveOne(DynamicArray<T>& items) {
        if (items.size() < items.capacity()) return;
        if (items.capacity() > std::numeric_limits<std::size_t>::max() / 2) {
            throw std::length_error("Graph storage capacity exhausted");
        }
        DynamicArray<T> replacement(items.empty() ? 4 : items.capacity() * 2);
        for (std::size_t i = 0; i < items.size(); ++i) {
            replacement.push_back(std::move(items[i]));
        }
        items = std::move(replacement);
    }

    Result<VertexId> insertVertex(VertexId id, TVertexData data) {
        if (!vertexCounter_.accepts(id)) {
            return failure(GraphErrorCode::InvalidParameters, "Vertex ID has already been issued or skipped.");
        }
        reserveOne(vertices_);
        reserveOne(adjacency_);
        auto indexed = addGraphDictionaryEntry(vertexIndex(), id, vertices_.size());
        if (!indexed) {
            return std::unexpected(indexed.error());
        }
        vertices_.push_back({id, std::move(data)});
        adjacency_.push_back({});
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

    Result<void> validateEdgeInsertion(VertexId first, VertexId second, EdgeId id) const {
        if (!containsVertex(first) || !containsVertex(second)) {
            return failure(GraphErrorCode::InvalidVertex);
        }
        if (first == second) {
            return failure(GraphErrorCode::SelfLoop);
        }
        if (hasEdge(first, second)) {
            return failure(GraphErrorCode::DuplicateEdge);
        }
        if (edgeCounter_.exhausted) {
            return failure(GraphErrorCode::IdentifierExhausted);
        }
        if (!edgeCounter_.accepts(id)) {
            return failure(GraphErrorCode::InvalidParameters, "Edge ID has already been issued or skipped.");
        }
        return {};
    }

    EdgeId nextEdgeId() const noexcept { return edgeCounter_.next; }

    Result<EdgeId> insertEdge(VertexId first, VertexId second, EdgeId id) {
        auto valid = validateEdgeInsertion(first, second, id);
        if (!valid) {
            return std::unexpected(valid.error());
        }
        auto& left = adjacency_[*vertexIndex().find(first)];
        auto& right = adjacency_[*vertexIndex().find(second)];
        reserveOne(edges_);
        reserveOne(left);
        reserveOne(right);
        auto indexed = addGraphDictionaryEntry(edgeIndex(), id, edges_.size());
        if (!indexed) {
            return std::unexpected(indexed.error());
        }
        edges_.push_back({id, first, second});
        left.push_back({second, id});
        right.push_back({first, id});
        edgeCounter_.consume(id);
        return id;
    }

public:
    virtual ~UndirectedGraphBase() = default;

    std::size_t vertexCount() const noexcept { return vertices_.size(); }
    std::size_t edgeCount() const noexcept { return edges_.size(); }
    bool containsVertex(VertexId id) const { return vertexIndex().containsKey(id); }
    bool containsEdge(EdgeId id) const { return edgeIndex().containsKey(id); }

    VertexId addVertex(TVertexData data) {
        if (vertexCounter_.exhausted) {
            throw std::overflow_error("Vertex identifier space exhausted");
        }
        auto added = insertVertex(vertexCounter_.next, std::move(data));
        if (!added) {
            throw std::logic_error(formatError(added.error()));
        }
        return *added;
    }

    Result<VertexId> addVertexWithId(VertexId id, TVertexData data) {
        return insertVertex(id, std::move(data));
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
    Result<std::reference_wrapper<const DynamicArray<AdjacencyEntry>>> neighbors(VertexId id) const {
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
    DynamicArray<VertexId> vertexIds() const { return vertexIndex().getKeys(); }
    DynamicArray<EdgeId> edgeIds() const { return edgeIndex().getKeys(); }

    Result<EdgeId> edgeId(VertexId first, VertexId second) const {
        if (!containsVertex(first) || !containsVertex(second)) {
            return failure(GraphErrorCode::InvalidVertex);
        }
        const auto& entries = neighbors(first)->get();
        for (const auto& entry : entries) {
            if (entry.neighbor == second) {
                return entry.edge;
            }
        }
        return failure(GraphErrorCode::MissingEdge);
    }
    bool hasEdge(VertexId first, VertexId second) const { return edgeId(first, second).has_value(); }

    Result<void> removeEdge(EdgeId id) {
        auto position = edgeIndex().find(id);
        if (!position) {
            return failure(GraphErrorCode::MissingEdge);
        }
        const auto removed = edges_[*position];
        eraseEdgeData(id);
        eraseNeighbor(adjacency_[*vertexIndex().find(removed.first)], id);
        eraseNeighbor(adjacency_[*vertexIndex().find(removed.second)], id);
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
            (void)removeEdge(entries[entries.size() - 1].edge);
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
            return proposed.next >= current.next && (!current.exhausted || proposed.exhausted) &&
                   (!proposed.exhausted || proposed.next == std::numeric_limits<std::size_t>::max());
        };
        if (!valid(vertexCounter_, vertices) || !valid(edgeCounter_, edges)) {
            return failure(GraphErrorCode::InvalidParameters, "Invalid identifier high-water marks.");
        }
        vertexCounter_ = vertices;
        edgeCounter_ = edges;
        return {};
    }
};

template <GraphData TVertexData>
class UnweightedUndirectedGraph final : public UndirectedGraphBase<TVertexData> {
public:
    UnweightedUndirectedGraph() = default;
    UnweightedUndirectedGraph(UnweightedUndirectedGraph&&) noexcept = default;
    UnweightedUndirectedGraph& operator=(UnweightedUndirectedGraph&&) noexcept = default;

    Result<EdgeId> addEdge(VertexId first, VertexId second) {
        return this->insertEdge(first, second, this->nextEdgeId());
    }
    Result<EdgeId> addEdgeWithId(EdgeId id, VertexId first, VertexId second) {
        return this->insertEdge(first, second, id);
    }
};

template <GraphData TVertexData, class TWeight>
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
        auto valid = this->validateEdgeInsertion(first, second, id);
        if (!valid) {
            return std::unexpected(valid.error());
        }
        auto added = addGraphDictionaryEntry(weights(), id, std::make_shared<const TWeight>(std::move(weight)));
        if (!added) {
            return std::unexpected(added.error());
        }
        try {
            auto edge = this->insertEdge(first, second, id);
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

using ColoringGraph = UnweightedUndirectedGraph<VertexData>;
using Graph = ColoringGraph;
