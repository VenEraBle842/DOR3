#include "sequence/array_sequence.hpp"
#include "sequence/sequence_heap_sort.hpp"
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

template <class T>
void expectContainerError(const ContainerResult<T>& result, ContainerErrorCode code) {
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, code);
}

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
    auto accessed = values.at(1);
    ASSERT_TRUE(accessed);
    accessed->get() = 7;
    ASSERT_TRUE(values.resize(8));
    EXPECT_EQ(values[1], 7);
    EXPECT_EQ(values[7], 0);
    ASSERT_TRUE(values.resize(2));
    EXPECT_EQ(values[1], 7);
    expectContainerError(values.at(2), ContainerErrorCode::IndexOutOfRange);
    ASSERT_TRUE(values.resize(0));
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
    ASSERT_TRUE(original.resize(1));
    original[0] = "reused";
    copy = std::move(moved);
    EXPECT_TRUE(moved.empty());
    EXPECT_EQ(copy[0], "copy");
}

TEST(ArraySequence, ReserveAndCountConstructorHaveDifferentContracts) {
    ArraySequence<int> values;
    ASSERT_TRUE(values.reserve(10));
    EXPECT_EQ(values.size(), 0);
    EXPECT_GE(values.capacity(), 10);
    expectContainerError(values.at(0), ContainerErrorCode::IndexOutOfRange);
    const auto capacity = values.capacity();
    ASSERT_TRUE(values.reserve(1));
    EXPECT_EQ(values.capacity(), capacity);
    ArraySequence<int> initialized(3);
    EXPECT_EQ(initialized.size(), 3);
    EXPECT_EQ(initialized[2], 0);
    ArraySequence<int> filled(3, 9);
    EXPECT_EQ(filled.at(2)->get(), 9);
}

TEST(ArraySequence, GrowsGeometricallyAndPreservesValuesIncludingAliasedAppend) {
    ArraySequence<std::string> values;
    int growths = 0;
    for (int i = 0; i < 1000; ++i) {
        auto before = values.capacity();
        ASSERT_TRUE(values.push_back(std::to_string(i)));
        if (before != values.capacity()) ++growths;
    }
    EXPECT_LT(growths, 12);
    for (int i = 0; i < 1000; ++i) EXPECT_EQ(values[static_cast<std::size_t>(i)], std::to_string(i));
    while (values.size() < values.capacity()) { ASSERT_TRUE(values.push_back("padding")); }
    ASSERT_TRUE(values.push_back(values[0]));
    EXPECT_EQ(values[values.size() - 1], "0");
}

TEST(ArraySequence, InsertAndErasePreserveOrderAndCheckBounds) {
    ArraySequence<int> values{2, 4};
    ASSERT_TRUE(values.insert(0, 1));
    ASSERT_TRUE(values.insert(2, 3));
    ASSERT_TRUE(values.insert(values.size(), 5));
    for (std::size_t i = 0; i < values.size(); ++i) EXPECT_EQ(values[i], i + 1);
    ASSERT_TRUE(values.erase(0));
    ASSERT_TRUE(values.erase(1));
    ASSERT_TRUE(values.erase(values.size() - 1));
    ASSERT_EQ(values.size(), 2);
    EXPECT_EQ(values[0], 2);
    EXPECT_EQ(values[1], 4);
    expectContainerError(values.insert(3, 0), ContainerErrorCode::IndexOutOfRange);
    expectContainerError(values.erase(2), ContainerErrorCode::IndexOutOfRange);
    values.clear();
    values.pop_back();
    expectContainerError(values.erase(0), ContainerErrorCode::IndexOutOfRange);
    ASSERT_TRUE(values.insert(0, 8));
    EXPECT_EQ(values[0], 8);
}

TEST(ArraySequence, DestroysRemovedResourcesAndMovesWithoutDefaultConstructingElements) {
    ArraySequence<std::shared_ptr<int>> values;
    auto first = std::make_shared<int>(1);
    auto second = std::make_shared<int>(2);
    ASSERT_TRUE(values.push_back(first));
    ASSERT_TRUE(values.push_back(second));
    values.pop_back();
    EXPECT_EQ(second.use_count(), 1);
    auto capacity = values.capacity();
    values.clear();
    EXPECT_EQ(first.use_count(), 1);
    EXPECT_EQ(values.capacity(), capacity);
    ArraySequence<std::unique_ptr<int>> owned;
    ASSERT_TRUE(owned.push_back(std::make_unique<int>(11)));
    ASSERT_TRUE(owned.reserve(100));
    ArraySequence<std::unique_ptr<int>> moved(std::move(owned));
    EXPECT_TRUE(owned.empty());
    EXPECT_EQ(owned.capacity(), 0);
    ASSERT_TRUE(owned.push_back(std::make_unique<int>(12)));
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
    ASSERT_TRUE(copy.reserve(30));
    int sum = 0;
    for (auto& value : copy) { sum += value; ++value; }
    EXPECT_EQ(sum, 6);
    EXPECT_EQ(source[0], 3);
    expectContainerError(source.at(3), ContainerErrorCode::IndexOutOfRange);
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

TEST(ContainerErrors, RecoverableFailuresPreserveContentsAndDescribeTheRequest) {
    ArraySequence<int> values{4, 8};
    const auto capacity = values.capacity();
    auto inserted = values.insert(3, 9);
    ASSERT_FALSE(inserted);
    EXPECT_EQ(inserted.error().requested, 3);
    EXPECT_EQ(inserted.error().size, 2);
    EXPECT_NE(formatError(inserted.error()).find("Index 3"), std::string::npos);
    expectContainerError(values.erase(2), ContainerErrorCode::IndexOutOfRange);
    expectContainerError(values.reserve(std::numeric_limits<std::size_t>::max()),
                         ContainerErrorCode::CapacityExceeded);
    EXPECT_EQ(values.size(), 2);
    EXPECT_EQ(values.capacity(), capacity);
    EXPECT_EQ(values[0], 4);
    EXPECT_EQ(values[1], 8);

    dor3::DynamicArray<int> array(2);
    array[0] = 17;
    expectContainerError(array.resize(std::numeric_limits<std::size_t>::max()),
                         ContainerErrorCode::CapacityExceeded);
    EXPECT_EQ(array.size(), 2);
    EXPECT_EQ(array[0], 17);
    const auto& constant = array;
    auto accessed = constant.at(0);
    ASSERT_TRUE(accessed);
    EXPECT_EQ(&accessed->get(), &array[0]);
    const auto& constantSequence = values;
    auto sequenceAccessed = constantSequence.at(1);
    ASSERT_TRUE(sequenceAccessed);
    EXPECT_EQ(&sequenceAccessed->get(), &values[1]);
}
