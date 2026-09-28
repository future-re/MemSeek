#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include "common/scan_test_helper.hpp"
#include "memseek/memory/read.hpp"
#include "memseek/memory/scan.hpp"
#include "memseek/value.hpp"

namespace memseek {

namespace {

template <ValueNumericType T>
void expectNumericOrdering(T lower, T target, T upper) {
    const auto scan = [](T memoryValue, const Value& bound, ScanType scanType) {
        auto data = test::encodeValue(memoryValue);
        const MemoryRead memory(0x6000, data.size(), std::move(data));
        return MemoryScanner::scanBuffer(memory, bound, scanType);
    };

    const auto scanRange = [](T memoryValue, const Value& lowerBound,
                              const Value& upperBound) {
        auto data = test::encodeValue(memoryValue);
        const MemoryRead memory(0x6000, data.size(), std::move(data));
        return MemoryScanner::scanBuffer(memory, lowerBound, upperBound);
    };

    EXPECT_EQ(scan(lower, Value(target), ScanType::LESS).size(), 1);
    EXPECT_TRUE(scan(lower, Value(target), ScanType::GREATER).empty());
    EXPECT_EQ(scan(upper, Value(target), ScanType::GREATER).size(), 1);
    EXPECT_TRUE(scan(upper, Value(target), ScanType::LESS).empty());

    EXPECT_EQ(scanRange(target, Value(lower), Value(upper)).size(), 1);
    EXPECT_TRUE(scanRange(upper, Value(lower), Value(target)).empty());
}

}  // namespace

TEST(NumericScanTest, SupportsLessGreaterAndInclusiveRange) {
    std::vector<std::byte> data;
    const auto one = test::encodeValue(std::uint8_t{1});
    const auto five = test::encodeValue(std::uint8_t{5});
    const auto nine = test::encodeValue(std::uint8_t{9});
    data.insert(data.end(), one.begin(), one.end());
    data.insert(data.end(), five.begin(), five.end());
    data.insert(data.end(), nine.begin(), nine.end());

    constexpr std::uintptr_t address = 0x3000;
    const MemoryRead memory(address, data.size(), std::move(data));

    const auto less = MemoryScanner::scanBuffer(memory, Value(std::uint8_t{5}),
                                                ScanType::LESS);
    ASSERT_EQ(less.size(), 1);
    EXPECT_EQ(less.front().address(), address);

    const auto greater = MemoryScanner::scanBuffer(
        memory, Value(std::uint8_t{5}), ScanType::GREATER);
    ASSERT_EQ(greater.size(), 1);
    EXPECT_EQ(greater.front().address(), address + 2);

    const auto range = MemoryScanner::scanBuffer(memory, Value(std::uint8_t{1}),
                                                 Value(std::uint8_t{5}));
    ASSERT_EQ(range.size(), 2);
    EXPECT_EQ(range[0].address(), address);
    EXPECT_EQ(range[1].address(), address + 1);
}

TEST(NumericScanTest, SupportsOrderingForEveryNumericValueType) {
    expectNumericOrdering<std::uint8_t>(1, 5, 9);
    expectNumericOrdering<std::uint16_t>(1, 5, 9);
    expectNumericOrdering<std::uint32_t>(1, 5, 9);
    expectNumericOrdering<std::uint64_t>(1, 5, 9);
    expectNumericOrdering<std::int8_t>(-9, -2, 5);
    expectNumericOrdering<std::int16_t>(-9, -2, 5);
    expectNumericOrdering<std::int32_t>(-9, -2, 5);
    expectNumericOrdering<std::int64_t>(-9, -2, 5);
    expectNumericOrdering<float>(-1.5F, 0.25F, 3.5F);
    expectNumericOrdering<double>(-1.5, 0.25, 3.5);
}

TEST(NumericScanTest, RejectsNonNumericValues) {
    const std::vector<std::byte> data{std::byte{'a'}, std::byte{'b'},
                                      std::byte{'c'}};
    const MemoryRead memory(0x4000, data.size(), data);

    EXPECT_TRUE(MemoryScanner::scanBuffer(memory, Value(std::string{"b"}),
                                          ScanType::GREATER)
                    .empty());
    EXPECT_TRUE(MemoryScanner::scanBuffer(
                    memory, Value(std::vector<std::byte>{std::byte{'a'}}),
                    ScanType::LESS)
                    .empty());
    EXPECT_TRUE(MemoryScanner::scanBuffer(memory, Value(std::string{"a"}),
                                          Value(std::string{"z"}))
                    .empty());
}

TEST(NumericScanTest, RejectsInvalidRanges) {
    const auto data = test::encodeValue(std::uint32_t{5});
    const MemoryRead memory(0x5000, data.size(), data);

    EXPECT_TRUE(MemoryScanner::scanBuffer(memory, Value(std::uint32_t{9}),
                                          Value(std::uint32_t{1}))
                    .empty());
    EXPECT_TRUE(MemoryScanner::scanBuffer(memory, Value(std::uint16_t{1}),
                                          Value(std::uint32_t{9}))
                    .empty());
    EXPECT_TRUE(
        MemoryScanner::scanBuffer(
            memory, Value(std::numeric_limits<float>::quiet_NaN()), Value(9.0F))
            .empty());
}

TEST(NumericScanTest, DoesNotMatchNaN) {
    const auto data =
        test::encodeValue(std::numeric_limits<float>::quiet_NaN());
    const MemoryRead memory(0x5000, data.size(), data);

    EXPECT_TRUE(
        MemoryScanner::scanBuffer(memory, Value(1.0F), ScanType::GREATER)
            .empty());
    EXPECT_TRUE(
        MemoryScanner::scanBuffer(memory, Value(1.0F), ScanType::LESS).empty());
    EXPECT_TRUE(
        MemoryScanner::scanBuffer(memory, Value(0.0F), Value(2.0F)).empty());
}

}  // namespace memseek
