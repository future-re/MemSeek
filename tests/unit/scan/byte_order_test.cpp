#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "memseek/memory/read.hpp"
#include "memseek/memory/scan.hpp"
#include "memseek/value.hpp"

namespace memseek {
namespace {

template <ValueNumericType T>
void checkByteOrder(T lower, T middle, T upper) {
    for (const auto order :
         {ByteOrder::NATIVE, ByteOrder::LITTLE, ByteOrder::BIG}) {
        const Value target(middle);
        std::vector<std::byte> bytes(target.data().begin(),
                                     target.data().end());
        if ((order == ByteOrder::BIG &&
             std::endian::native == std::endian::little) ||
            (order == ByteOrder::LITTLE &&
             std::endian::native == std::endian::big)) {
            std::ranges::reverse(bytes);
        }
        const MemoryRead memory(0x1000, bytes.size(), bytes);
        const auto exact =
            MemoryScanner::scanBuffer(memory, target, ScanType::EXACT, order);
        ASSERT_EQ(exact.size(), 1);
        EXPECT_TRUE(std::ranges::equal(exact.at(0).data(), bytes));
        EXPECT_EQ(MemoryScanner::scanBuffer(memory, Value(lower),
                                            ScanType::GREATER, order)
                      .size(),
                  1);
        EXPECT_EQ(MemoryScanner::scanBuffer(memory, Value(upper),
                                            ScanType::LESS, order)
                      .size(),
                  1);
        EXPECT_EQ(
            MemoryScanner::scanBuffer(memory, Value(lower), Value(upper), order)
                .size(),
            1);
        EXPECT_TRUE(
            MemoryScanner::scanBuffer(memory, target, ScanType::GREATER, order)
                .empty());
        EXPECT_TRUE(
            MemoryScanner::scanBuffer(memory, Value(upper), Value(lower), order)
                .empty());
    }
}

TEST(ByteOrderTest, SupportsAllNumericTypesAndRetainsRawBytes) {
    checkByteOrder<std::uint8_t>(1, 2, 3);
    checkByteOrder<std::uint16_t>(0x102, 0x203, 0x304);
    checkByteOrder<std::uint32_t>(0x10203, 0x20304, 0x30405);
    checkByteOrder<std::uint64_t>(0x102030405ULL, 0x203040506ULL,
                                  0x304050607ULL);
    checkByteOrder<std::int8_t>(-3, -2, -1);
    checkByteOrder<std::int16_t>(-300, -200, -100);
    checkByteOrder<std::int32_t>(-300000, -200000, -100000);
    checkByteOrder<std::int64_t>(-30000000000LL, -20000000000LL,
                                 -10000000000LL);
    checkByteOrder<float>(-3.5F, -2.25F, -1.5F);
    checkByteOrder<double>(-3.5, -2.25, -1.5);
}

TEST(ByteOrderTest, ExplicitOrderMatchesKnownBytes) {
    const MemoryRead memory(0x1000, 4,
                            {
                                std::byte{0x12},
                                std::byte{0x34},
                                std::byte{0x56},
                                std::byte{0x78},
                            });
    const Value target(std::uint32_t{0x12345678});
    EXPECT_EQ(MemoryScanner::scanBuffer(memory, target, ScanType::EXACT,
                                        ByteOrder::BIG)
                  .size(),
              1);
    EXPECT_TRUE(MemoryScanner::scanBuffer(memory, target, ScanType::EXACT,
                                          ByteOrder::LITTLE)
                    .empty());
}

TEST(ByteOrderTest, DoesNotReverseStringsOrByteArrays) {
    const std::vector<std::byte> bytes{
        std::byte{'a'},
        std::byte{'b'},
    };
    const MemoryRead memory(0x1000, bytes.size(), bytes);
    for (const auto order : {ByteOrder::LITTLE, ByteOrder::BIG}) {
        EXPECT_EQ(MemoryScanner::scanBuffer(memory, Value(std::string{"ab"}),
                                            ScanType::EXACT, order)
                      .size(),
                  1);
        EXPECT_EQ(MemoryScanner::scanBuffer(memory, Value(bytes),
                                            ScanType::EXACT, order)
                      .size(),
                  1);
    }
}

}  // namespace
}  // namespace memseek
