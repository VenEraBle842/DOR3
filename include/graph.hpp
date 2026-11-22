#pragma once

#include "errors.hpp"
#include <concepts>
#include <containers/dynamic_array.hpp>
#include <cstddef>
#include <functional>
#include <string>
#include <type_traits>
#include <utility>

using VertexId = std::size_t;

struct VertexData {
    std::string label;
};

template <typename TData>
requires std::default_initializable<TData> && std::movable<TData> &&
         std::is_nothrow_move_constructible_v<TData> && std::is_nothrow_move_assignable_v<TData>
class UndirectedGraph {
    DynamicArray<TData> vertices_;
    DynamicArray<DynamicArray<VertexId>> adjacency_;
    std::size_t edgeCount_ = 0;

    static void eraseNeighbor(DynamicArray<VertexId>& neighbors, VertexId target) {
        for (std::size_t i = 0; i < neighbors.size(); ++i) {
            if (neighbors[i] == target) {
                neighbors[i] = neighbors[neighbors.size() - 1];
                neighbors.pop_back();
                return;
            }
        }
    }

    void reserveVertexStorage() {
        if (vertices_.size() < vertices_.capacity() && adjacency_.size() < adjacency_.capacity()) {
            return;
        }

        const auto capacity = vertexCount() == 0 ? 4 : vertexCount() * 2;
        DynamicArray<TData> vertices(capacity);
        DynamicArray<DynamicArray<VertexId>> adjacency(capacity);

        for (std::size_t i = 0; i < vertexCount(); ++i) {
            vertices.push_back(std::move(vertices_[i]));
            adjacency.push_back(std::move(adjacency_[i]));
        }

        vertices_ = std::move(vertices);
        adjacency_ = std::move(adjacency);
    }

    static void reserveNeighborStorage(DynamicArray<VertexId>& neighbors) {
        if (neighbors.size() < neighbors.capacity()) {
            return;
        }

        const auto capacity = neighbors.empty() ? 4 : neighbors.size() * 2;
        DynamicArray<VertexId> replacement(capacity);

        for (std::size_t i = 0; i < neighbors.size(); ++i) {
            replacement.push_back(neighbors[i]);
        }

        neighbors = std::move(replacement);
    }

public:
    UndirectedGraph() = default;
    UndirectedGraph(const UndirectedGraph&) = delete;
    UndirectedGraph& operator=(const UndirectedGraph&) = delete;

    UndirectedGraph(UndirectedGraph&& other) noexcept
        : vertices_(std::move(other.vertices_)), adjacency_(std::move(other.adjacency_)),
          edgeCount_(std::exchange(other.edgeCount_, 0)) {
    }

    UndirectedGraph& operator=(UndirectedGraph&& other) noexcept {
        if (this != &other) {
            vertices_ = std::move(other.vertices_);
            adjacency_ = std::move(other.adjacency_);
            edgeCount_ = std::exchange(other.edgeCount_, 0);
        }
        return *this;
    }

    std::size_t vertexCount() const noexcept {
        return vertices_.size();
    }

    std::size_t edgeCount() const noexcept {
        return edgeCount_;
    }

    bool containsVertex(VertexId vertex) const noexcept {
        return vertex < vertexCount();
    }

    VertexId addVertex(TData data) {
        // Выделение обоих массивов прежде изменения количества вершин графа
        reserveVertexStorage();
        vertices_.push_back(std::move(data));
        adjacency_.push_back({});
        return vertexCount() - 1;
    }

    Result<std::reference_wrapper<const TData>> vertex(VertexId id) const {
        if (!containsVertex(id)) {
            return failure(ErrorCode::InvalidVertex);
        }
        return std::cref(vertices_[id]);
    }

    Result<std::reference_wrapper<const DynamicArray<VertexId>>> neighbors(VertexId id) const {
        if (!containsVertex(id)) {
            return failure(ErrorCode::InvalidVertex);
        }
        return std::cref(adjacency_[id]);
    }

    bool hasEdge(VertexId first, VertexId second) const noexcept {
        if (!containsVertex(first) || !containsVertex(second)) {
            return false;
        }
        for (std::size_t i = 0; i < adjacency_[first].size(); ++i) {
            if (adjacency_[first][i] == second) {
                return true;
            }
        }
        return false;
    }

    Result<void> addEdge(VertexId first, VertexId second) {
        if (!containsVertex(first) || !containsVertex(second)) {
            return failure(ErrorCode::InvalidVertex);
        }
        if (first == second) {
            return failure(ErrorCode::SelfLoop);
        }
        if (hasEdge(first, second)) {
            return failure(ErrorCode::DuplicateEdge);
        }
        auto& left = adjacency_[first];
        auto& right = adjacency_[second];
        // Геометрический рост емкости сохраняет амортизированную стоимость вставки
        // Память для обоих списков выделяется до добавления связей, сохраняя симметрию при сбое
        reserveNeighborStorage(left);
        reserveNeighborStorage(right);
        left.push_back(second);
        right.push_back(first);
        ++edgeCount_;
        return {};
    }

    Result<void> removeEdge(VertexId first, VertexId second) {
        if (!containsVertex(first) || !containsVertex(second)) {
            return failure(ErrorCode::InvalidVertex);
        }
        if (!hasEdge(first, second)) {
            return failure(ErrorCode::MissingEdge);
        }
        eraseNeighbor(adjacency_[first], second);
        eraseNeighbor(adjacency_[second], first);
        --edgeCount_;
        return {};
    }
};

using Graph = UndirectedGraph<VertexData>;
