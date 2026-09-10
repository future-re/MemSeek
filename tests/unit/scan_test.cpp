#include "memseek/scan.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "common/test_helper.hpp"
#include "memseek/memory_read.hpp"

namespace memseek {
TEST(ScanTest, valueScan) {
    uint64_t address = 0x1000;
    std::vector<std::byte> data{
        static_cast<std::byte>(0x12), static_cast<std::byte>(0x34),
        static_cast<std::byte>(0x56), static_cast<std::byte>(0x78),
        static_cast<std::byte>(0x99)};
    auto test = memseek::MemoryRead(address, data);
    Value target(static_cast<uint16_t>(0x7856));
    auto result = MemoryScanner::scanExact(test, target);
    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result[0].address(), address + 2);
    EXPECT_EQ(result[0].data(), target);
}

TEST(ScanTest, mulipleValueScan) {
    uint64_t address = 0x2000;
    std::vector<std::byte> data;
    data += changeValue(uint32_t(45));
    data += changeValue(uint32_t(36));
    data+= changeValue(uint32_t(45));
    auto test = memseek::MemoryRead(address, data);
    Value target(static_cast<uint32_t>(45));
    auto result = MemoryScanner::scanExact(test, target);
    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0].data(), target);
    EXPECT_EQ(result[1].data(), target);
    ASSERT_EQ(MemoryScanner::scanExact(test, Value(static_cast<uint32_t>(36))).size(), 1);
}

TEST(ScanTest, stringScan) {
    uint64_t address = 0x2000;
    std::vector<std::byte> data{
        static_cast<std::byte>('H'), static_cast<std::byte>('e'),
        static_cast<std::byte>('l'), static_cast<std::byte>('l'),
        static_cast<std::byte>('o'), static_cast<std::byte>(' '),
        static_cast<std::byte>('W'), static_cast<std::byte>('o'),
        static_cast<std::byte>('r'), static_cast<std::byte>('l'),
        static_cast<std::byte>('d')};
    auto test = memseek::MemoryRead(address, data);
    Value target(std::string("lo Wo"));
    auto result = MemoryScanner::scanExact(test, target);
    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result[0].address(), address + 3);
    EXPECT_EQ(result[0].data(), target);
}
}  // namespace memseek