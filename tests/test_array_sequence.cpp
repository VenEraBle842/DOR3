#include "array_sequence.hpp"
#include "sequence_heap_sort.hpp"
#include <containers/dynamic_array.hpp>
#include <common/heap_sort.hpp>
#include <gtest/gtest.h>
#include <memory>
#include <string>

namespace {
template <class T>
concept HasPushBack = requires(T& values) { values.push_back(1); };
static_assert(!HasPushBack<dor3::DynamicArray<int>>);
static_assert(HasPushBack<ArraySequence<int>>);
static_assert(!std::is_copy_constructible_v<ArraySequence<std::unique_ptr<int>>>);

struct ThrowingValue {
    inline static int copiesLeft = -1;
    inline static bool failDefault = false;
    int value = 0;
    ThrowingValue() { if (failDefault) throw std::runtime_error("default"); }
    explicit ThrowingValue(int number) : value(number) {}
    ThrowingValue(const ThrowingValue& other) { *this = other; }
    ThrowingValue& operator=(const ThrowingValue& other) {
        if (copiesLeft == 0) throw std::runtime_error("copy");
        if (copiesLeft > 0) --copiesLeft;
        value = other.value;
        return *this;
    }
    ThrowingValue(ThrowingValue&& other) noexcept(false) : value(other.value) {}
    ThrowingValue& operator=(ThrowingValue&& other) noexcept(false) {
        value = other.value;
        return *this;
    }
};
}

TEST(DynamicArray, SizeIsAccessibleInitializedStorageAndResizePreservesPrefix) {
    dor3::DynamicArray<int> values(3);
    EXPECT_EQ(values.size(), 3);
    EXPECT_EQ(values[2], 0);
    values.at(1) = 7;
    values.resize(8);
    EXPECT_EQ(values[1], 7);
    EXPECT_EQ(values[7], 0);
    values.resize(2);
    EXPECT_EQ(values[1], 7);
    EXPECT_THROW(values.at(2), std::out_of_range);
    values.resize(0);
    EXPECT_TRUE(values.empty());
}

TEST(DynamicArray, CopiesAreIndependentAndMovedSourceCanBeReused) {
    dor3::DynamicArray<std::string> original(2);
    original[0] = "first";
    auto copy = original;
    copy[0] = "copy";
    EXPECT_EQ(original[0], "first");
    original = copy;
    dor3::DynamicArray<std::string> moved(std::move(original));
    EXPECT_TRUE(original.empty());
    original.resize(1);
    original[0] = "reused";
    copy = std::move(moved);
    EXPECT_TRUE(moved.empty());
    EXPECT_EQ(copy[0], "copy");
}

TEST(DynamicArray, FailedGrowthPreservesSizeAndValues) {
    dor3::DynamicArray<ThrowingValue> values(2);
    values[0].value = 19;
    ThrowingValue::failDefault = true;
    EXPECT_THROW(values.resize(10), std::runtime_error);
    ThrowingValue::failDefault = false;
    EXPECT_EQ(values.size(), 2);
    EXPECT_EQ(values[0].value, 19);
    ThrowingValue::copiesLeft = 1;
    EXPECT_THROW(values.resize(10), std::runtime_error);
    ThrowingValue::copiesLeft = -1;
    EXPECT_EQ(values.size(), 2);
    EXPECT_EQ(values[0].value, 19);
}

TEST(ArraySequence, ReserveAndCountConstructorHaveDifferentContracts) {
    ArraySequence<int> values;
    values.reserve(10);
    EXPECT_EQ(values.size(), 0);
    EXPECT_GE(values.capacity(), 10);
    EXPECT_THROW(values.at(0), std::out_of_range);
    const auto capacity = values.capacity();
    values.reserve(1);
    EXPECT_EQ(values.capacity(), capacity);
    ArraySequence<int> initialized(3);
    EXPECT_EQ(initialized.size(), 3);
    EXPECT_EQ(initialized[2], 0);
    ArraySequence<int> filled(3, 9);
    EXPECT_EQ(filled.at(2), 9);
}

TEST(ArraySequence, GrowsGeometricallyAndPreservesValuesIncludingAliasedAppend) {
    ArraySequence<std::string> values;
    int growths = 0;
    for (int i = 0; i < 1000; ++i) {
        auto before = values.capacity();
        values.push_back(std::to_string(i));
        if (before != values.capacity()) ++growths;
    }
    EXPECT_LT(growths, 12);
    for (int i = 0; i < 1000; ++i) EXPECT_EQ(values[static_cast<std::size_t>(i)], std::to_string(i));
    while (values.size() < values.capacity()) values.push_back("padding");
    values.push_back(values[0]);
    EXPECT_EQ(values[values.size() - 1], "0");
}

TEST(ArraySequence, InsertAndErasePreserveOrderAndCheckBounds) {
    ArraySequence<int> values{2, 4};
    values.insert(0, 1);
    values.insert(2, 3);
    values.insert(values.size(), 5);
    for (std::size_t i = 0; i < values.size(); ++i) EXPECT_EQ(values[i], i + 1);
    values.erase(0);
    values.erase(1);
    values.erase(values.size() - 1);
    ASSERT_EQ(values.size(), 2);
    EXPECT_EQ(values[0], 2);
    EXPECT_EQ(values[1], 4);
    EXPECT_THROW(values.insert(3, 0), std::out_of_range);
    EXPECT_THROW(values.erase(2), std::out_of_range);
    values.clear();
    values.pop_back();
    EXPECT_THROW(values.erase(0), std::out_of_range);
    values.insert(0, 8);
    EXPECT_EQ(values[0], 8);
}

TEST(ArraySequence, DestroysRemovedResourcesAndMovesWithoutDefaultConstructingElements) {
    ArraySequence<std::shared_ptr<int>> values;
    auto first = std::make_shared<int>(1);
    auto second = std::make_shared<int>(2);
    values.push_back(first);
    values.push_back(second);
    values.pop_back();
    EXPECT_EQ(second.use_count(), 1);
    auto capacity = values.capacity();
    values.clear();
    EXPECT_EQ(first.use_count(), 1);
    EXPECT_EQ(values.capacity(), capacity);
    ArraySequence<std::unique_ptr<int>> owned;
    owned.push_back(std::make_unique<int>(11));
    owned.reserve(100);
    ArraySequence<std::unique_ptr<int>> moved(std::move(owned));
    EXPECT_TRUE(owned.empty());
    EXPECT_EQ(owned.capacity(), 0);
    owned.push_back(std::make_unique<int>(12));
    moved = std::move(owned);
    EXPECT_TRUE(owned.empty());
    EXPECT_EQ(*moved[0], 12);
}

TEST(ArraySequence, CopiesAreIndependentAndIterationUsesLogicalSize) {
    const ArraySequence<int> source{3, 1, 2};
    auto copy = source;
    copy[0] = 99;
    EXPECT_EQ(source[0], 3);
    copy = source;
    copy.reserve(30);
    int sum = 0;
    for (auto& value : copy) { sum += value; ++value; }
    EXPECT_EQ(sum, 6);
    EXPECT_EQ(source[0], 3);
    EXPECT_THROW(source.at(3), std::out_of_range);
}

TEST(ArraySequence, FailedReserveOrCopyAssignmentPreservesOriginalValues) {
    ArraySequence<ThrowingValue> source(3);
    source[0].value = 42;
    ThrowingValue::copiesLeft = 1;
    EXPECT_THROW(source.reserve(100), std::runtime_error);
    EXPECT_EQ(source.size(), 3);
    EXPECT_EQ(source[0].value, 42);
    ThrowingValue::copiesLeft = -1;
    ArraySequence<ThrowingValue> destination(1);
    destination[0].value = 7;
    ThrowingValue::copiesLeft = 1;
    EXPECT_THROW(destination = source, std::runtime_error);
    ThrowingValue::copiesLeft = -1;
    EXPECT_EQ(destination.size(), 1);
    EXPECT_EQ(destination[0].value, 7);
    EXPECT_THROW(source.reserve(std::numeric_limits<std::size_t>::max()), std::length_error);
}

TEST(ArraySequence, CoexistsWithLegacyArrayAndBothHeapSortOverloads) {
    ::DynamicArray<int> legacy{3, 1, 2};
    ArraySequence<int> current{3, 1, 2};
    heapSort(legacy);
    heapSort(current);
    for (std::size_t i = 0; i < 3; ++i) EXPECT_EQ(current[i], legacy[i]);
    heapSort(current, std::greater<int>{});
    EXPECT_EQ(current[0], 3);
    ArraySequence<int> empty;
    heapSort(empty);
}
