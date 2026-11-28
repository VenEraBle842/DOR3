#pragma once

#include "container_errors.hpp"
#include <concepts>
#include <functional>
#include <cstddef>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <utility>

namespace dor3 {

template <class T>
class DynamicArray {
    std::unique_ptr<T[]> data_;
    std::size_t size_ = 0;

public:
    DynamicArray() = default;
    explicit DynamicArray(std::size_t size)
        : data_(size ? std::make_unique<T[]>(size) : nullptr), size_(size) {}

    DynamicArray(const DynamicArray& other) requires std::copyable<T>
        : DynamicArray(other.size_) {
        for (std::size_t i = 0; i < size_; ++i) data_[i] = other[i];
    }
    DynamicArray& operator=(const DynamicArray& other) requires std::copyable<T> {
        if (this != &other) {
            DynamicArray copy(other);
            *this = std::move(copy);
        }
        return *this;
    }
    DynamicArray(DynamicArray&& other) noexcept
        : data_(std::move(other.data_)), size_(std::exchange(other.size_, 0)) {}
    DynamicArray& operator=(DynamicArray&& other) noexcept {
        if (this != &other) {
            data_ = std::move(other.data_);
            size_ = std::exchange(other.size_, 0);
        }
        return *this;
    }
    ContainerResult<void> resize(std::size_t size) {
        const auto maximum = static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max()) / sizeof(T);
        if (size > maximum) return containerFailure(ContainerErrorCode::CapacityExceeded, size, maximum);
        if (size == size_) return {};
        DynamicArray replacement(size);
        for (std::size_t i = 0; i < size && i < size_; ++i) {
            replacement[i] = std::move_if_noexcept(data_[i]);
        }
        *this = std::move(replacement);
        return {};
    }
    std::size_t size() const noexcept { return size_; }
    bool empty() const noexcept { return size_ == 0; }
    T& operator[](std::size_t index) noexcept { return data_[index]; }
    const T& operator[](std::size_t index) const noexcept { return data_[index]; }
    ContainerResult<std::reference_wrapper<T>> at(std::size_t index) {
        if (index >= size_) return containerFailure(ContainerErrorCode::IndexOutOfRange, index, size_);
        return std::ref(data_[index]);
    }
    ContainerResult<std::reference_wrapper<const T>> at(std::size_t index) const {
        if (index >= size_) return containerFailure(ContainerErrorCode::IndexOutOfRange, index, size_);
        return std::cref(data_[index]);
    }
};

} // namespace dor3

template <class T>
class ArraySequence {
    dor3::DynamicArray<std::optional<T>> buffer_;
    std::size_t size_ = 0;

public:
    ArraySequence() = default;
    explicit ArraySequence(std::size_t count) : buffer_(count) {
        while (size_ < count) {
            buffer_[size_].emplace();
            ++size_;
        }
    }
    ArraySequence(std::size_t count, const T& value) : buffer_(count) {
        while (size_ < count) {
            buffer_[size_].emplace(value);
            ++size_;
        }
    }
    ArraySequence(std::initializer_list<T> values) : buffer_(values.size()) {
        for (const auto& value : values) {
            buffer_[size_].emplace(value);
            ++size_;
        }
    }
    ArraySequence(const ArraySequence&) requires std::copyable<T> = default;
    ArraySequence& operator=(const ArraySequence& other)
        requires std::copyable<T> {
        if (this != &other) {
            ArraySequence copy(other);
            *this = std::move(copy);
        }
        return *this;
    }
    ArraySequence(ArraySequence&& other) noexcept
        : buffer_(std::move(other.buffer_)), size_(std::exchange(other.size_, 0)) {}
    ArraySequence& operator=(ArraySequence&& other) noexcept {
        if (this != &other) {
            buffer_ = std::move(other.buffer_);
            size_ = std::exchange(other.size_, 0);
        }
        return *this;
    }
    std::size_t size() const noexcept { return size_; }
    std::size_t capacity() const noexcept { return buffer_.size(); }
    bool empty() const noexcept { return size_ == 0; }
    ContainerResult<void> reserve(std::size_t requested) {
        if (requested <= capacity()) return {};
        constexpr auto maximum = static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max()) / sizeof(std::optional<T>);
        if (requested > maximum)
            return containerFailure(ContainerErrorCode::CapacityExceeded, requested, maximum);
        auto next = capacity() ? capacity() : (maximum < 4 ? maximum : 4);
        while (next < requested) next = next > maximum / 2 ? maximum : next * 2;
        return buffer_.resize(next);
    }
    ContainerResult<void> push_back(T value) {
        auto reserved = reserve(size_ + 1);
        if (!reserved) return reserved;
        buffer_[size_].emplace(std::move(value));
        ++size_;
        return {};
    }
    void pop_back() noexcept {
        if (size_) buffer_[--size_].reset();
    }
    void clear() noexcept {
        while (size_) pop_back();
    }
    ContainerResult<void> insert(std::size_t index, T value) {
        if (index > size_) return containerFailure(ContainerErrorCode::IndexOutOfRange, index, size_);
        auto appended = push_back(std::move(value));
        if (!appended) return appended;
        for (std::size_t i = size_ - 1; i > index; --i) {
            std::swap((*this)[i], (*this)[i - 1]);
        }
        return {};
    }
    ContainerResult<void> erase(std::size_t index) {
        if (index >= size_) return containerFailure(ContainerErrorCode::IndexOutOfRange, index, size_);
        for (std::size_t i = index; i + 1 < size_; ++i) (*this)[i] = std::move((*this)[i + 1]);
        pop_back();
        return {};
    }
    T& operator[](std::size_t index) noexcept { return *buffer_[index]; }
    const T& operator[](std::size_t index) const noexcept { return *buffer_[index]; }
    ContainerResult<std::reference_wrapper<T>> at(std::size_t index) {
        if (index >= size_) return containerFailure(ContainerErrorCode::IndexOutOfRange, index, size_);
        return std::ref((*this)[index]);
    }
    ContainerResult<std::reference_wrapper<const T>> at(std::size_t index) const {
        if (index >= size_) return containerFailure(ContainerErrorCode::IndexOutOfRange, index, size_);
        return std::cref((*this)[index]);
    }

    class Iterator {
        ArraySequence& sequence_;
        std::size_t index_;
    public:
        Iterator(ArraySequence& sequence, std::size_t index) : sequence_(sequence), index_(index) {}
        T& operator*() const { return sequence_[index_]; }
        Iterator& operator++() { ++index_; return *this; }
        bool operator!=(const Iterator& other) const { return index_ != other.index_; }
    };
    class ConstIterator {
        const ArraySequence& sequence_;
        std::size_t index_;
    public:
        ConstIterator(const ArraySequence& sequence, std::size_t index) : sequence_(sequence), index_(index) {}
        const T& operator*() const { return sequence_[index_]; }
        ConstIterator& operator++() { ++index_; return *this; }
        bool operator!=(const ConstIterator& other) const { return index_ != other.index_; }
    };
    Iterator begin() noexcept { return {*this, 0}; }
    Iterator end() noexcept { return {*this, size_}; }
    ConstIterator begin() const noexcept { return {*this, 0}; }
    ConstIterator end() const noexcept { return {*this, size_}; }
};
