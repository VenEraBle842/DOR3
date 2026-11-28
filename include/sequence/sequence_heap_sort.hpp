#pragma once

#include "array_sequence.hpp"
#include <concepts>

#include <functional>
#include <utility>

template <typename T, typename Compare = std::less<T>>
requires std::strict_weak_order<Compare, const T&, const T&>
void heapSort(ArraySequence<T>& values, Compare compare = {}) {
    const auto siftDown = [&](std::size_t root, std::size_t count) {
        while (root < count / 2) {
            std::size_t child = root * 2 + 1;
            if (child + 1 < count && compare(values[child], values[child + 1])) ++child;
            if (!compare(values[root], values[child])) break;
            std::swap(values[root], values[child]);
            root = child;
        }
    };

    for (std::size_t root = values.size() / 2; root > 0; --root) siftDown(root - 1, values.size());
    for (std::size_t count = values.size(); count > 1; --count) {
        std::swap(values[0], values[count - 1]);
        siftDown(0, count - 1);
    }
}
