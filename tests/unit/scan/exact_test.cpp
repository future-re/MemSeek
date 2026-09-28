#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "common/scan_test_helper.hpp"
#include "memseek/memory/read.hpp"
#include "memseek/memory/scan.hpp"
#include "memseek/value.hpp"

namespace memseek {

namespace {

template <ValueSupportedType T>
void expectExactMatchFor(T&& value, std::string_view typeName) {
    constexpr std::uintptr_t address = 0x1000;
    const Value target(std::forward<T>(value));
    auto data =
        std::vector<std::byte>(target.data().begin(), target.data().end());
    const MemoryRead memory(address, data.size(), std::move(data));

    SCOPED_TRACE(typeName);
    const auto results = MemoryScanner::scanBuffer(memory, target);

    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results.front().address(), address);
    EXPECT_TRUE(target == results.front().data());
}

}  // namespace

TEST(ExactScanTest, SupportsEveryValueType) {
    expectExactMatchFor(std::uint8_t{0xa5}, "uint8_t");
    expectExactMatchFor(std::uint16_t{0x7a5c}, "uint16_t");
    expectExactMatchFor(std::uint32_t{0x78563412}, "uint32_t");
    expectExactMatchFor(std::uint64_t{0x8877665544332211ULL}, "uint64_t");
    expectExactMatchFor(std::int8_t{-42}, "int8_t");
    expectExactMatchFor(std::int16_t{-12345}, "int16_t");
    expectExactMatchFor(std::int32_t{-123456789}, "int32_t");
    expectExactMatchFor(std::int64_t{-1234567890123456789LL}, "int64_t");
    expectExactMatchFor(3.25F, "float");
    expectExactMatchFor(-6.5, "double");
    expectExactMatchFor(std::string{"mem\0seek", 8}, "string with NUL");
    expectExactMatchFor(std::vector<std::byte>{std::byte{0x00}, std::byte{0xff},
                                               std::byte{0x7f}},
                        "byte array");
}

TEST(ExactScanTest, FindsOverlappingMatchesAtFirstAndLastOffset) {
    constexpr std::uintptr_t address = 0x1100;
    const Value target(std::string{"aba"});
    auto data = test::encodeValue(std::string{"ababa"});
    const MemoryRead memory(address, data.size(), std::move(data));

    const auto results = MemoryScanner::scanBuffer(memory, target);

    ASSERT_EQ(results.size(), 2);
    EXPECT_EQ(results[0].address(), address);
    EXPECT_EQ(results[1].address(), address + 2);
    EXPECT_TRUE(target == results[0].data());
    EXPECT_TRUE(target == results[1].data());
}

TEST(ExactScanTest, FindsUnalignedNumericValue) {
    constexpr std::uintptr_t address = 0x1200;
    constexpr std::uint32_t targetValue = 0x78563412;
    const Value target(targetValue);

    std::vector<std::byte> data{std::byte{0x00}};
    const auto encodedTarget = test::encodeValue(targetValue);
    data.insert(data.end(), encodedTarget.begin(), encodedTarget.end());
    data.push_back(std::byte{0xff});

    const MemoryRead memory(address, data.size(), std::move(data));
    const auto results = MemoryScanner::scanBuffer(memory, target);

    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results.front().address(), address + 1);
    EXPECT_TRUE(target == results.front().data());
}

TEST(ExactScanTest, RejectsEmptyTargetAndInsufficientInput) {
    const MemoryRead emptyMemory(0x1300, 0, {});
    const MemoryRead shortMemory(0x1400, 2, {std::byte{0x01}, std::byte{0x02}});

    EXPECT_TRUE(
        MemoryScanner::scanBuffer(emptyMemory, Value(std::uint8_t{1})).empty());
    EXPECT_TRUE(
        MemoryScanner::scanBuffer(shortMemory, Value(std::string{})).empty());
    EXPECT_TRUE(MemoryScanner::scanBuffer(
                    shortMemory,
                    Value(std::vector<std::byte>{
                        std::byte{0x01}, std::byte{0x02}, std::byte{0x03}}))
                    .empty());
}

TEST(ExactScanTest, ReturnsNoPartialMatches) {
    const Value target(std::string{"abcd"});
    auto data = test::encodeValue(std::string{"abc"});
    const MemoryRead memory(0x1500, data.size(), std::move(data));

    EXPECT_TRUE(MemoryScanner::scanBuffer(memory, target).empty());
}

}  // namespace memseek
