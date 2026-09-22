#include <unistd.h>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

#include "common/test_helper.hpp"
#include "memseek/memory_chunk.hpp"
#include "memseek/memory_read.hpp"
#include "memseek/memory_region.hpp"
#include "memseek/memory_scan.hpp"
#include "memseek/value.hpp"

namespace memseek {
namespace {

MemoryRegion findRegion(std::uintptr_t address) {
    auto regions = readProcess(getpid());
    if (!regions) {
        return {};
    }
    for (const auto& region : regions->getRegions()) {
        if (address >= region.start && address - region.start < region.size) {
            return region;
        }
    }
    return {};
}

}  // namespace

TEST(ScanBufferTest, valueScan) {
    uint64_t address = 0x1000;
    std::vector<std::byte> data{
        static_cast<std::byte>(0x12), static_cast<std::byte>(0x34),
        static_cast<std::byte>(0x56), static_cast<std::byte>(0x78),
        static_cast<std::byte>(0x99)};
    auto test = MemoryRead(address, data.size(), std::move(data));
    Value target(static_cast<uint16_t>(0x7856));
    auto result = MemoryScanner::scanBuffer(test, target);
    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result[0].address(), address + 2);
    EXPECT_EQ(result[0].data(), target);
}

TEST(ScanBufferTest, multipleValueScan) {
    uint64_t address = 0x2000;
    std::vector<std::byte> data;
    data += changeValue(uint32_t(45));
    data += changeValue(uint32_t(36));
    data += changeValue(uint32_t(45));
    auto test = MemoryRead(address, data.size(), std::move(data));
    Value target(static_cast<uint32_t>(45));
    auto result = MemoryScanner::scanBuffer(test, target);
    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0].data(), target);
    EXPECT_EQ(result[1].data(), target);
    ASSERT_EQ(MemoryScanner::scanBuffer(test, Value(static_cast<uint32_t>(36)))
                  .size(),
              1);
}

TEST(ScanBufferTest, stringScan) {
    uint64_t address = 0x2000;
    std::vector<std::byte> data{
        static_cast<std::byte>('H'), static_cast<std::byte>('e'),
        static_cast<std::byte>('l'), static_cast<std::byte>('l'),
        static_cast<std::byte>('o'), static_cast<std::byte>(' '),
        static_cast<std::byte>('W'), static_cast<std::byte>('o'),
        static_cast<std::byte>('r'), static_cast<std::byte>('l'),
        static_cast<std::byte>('d')};
    auto test = MemoryRead(address, data.size(), std::move(data));
    Value target(std::string("lo Wo"));
    auto result = MemoryScanner::scanBuffer(test, target);
    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result[0].address(), address + 3);
    EXPECT_EQ(result[0].data(), target);
}

TEST(ScanRegionTest, findsValueSpanningChunkBoundary) {
    const std::uint32_t value = 0x11223344;
    std::vector<std::byte> buffer(2 * DEFAULT_CHUNK_SIZE, std::byte{0});

    const std::size_t offset = DEFAULT_CHUNK_SIZE - sizeof(value) / 2;
    std::memcpy(buffer.data() + offset, &value, sizeof(value));

    const auto address = reinterpret_cast<std::uintptr_t>(buffer.data());
    const auto region = findRegion(address);
    ASSERT_NE(region.size, 0);

    const auto results =
        MemoryScanner::scanRegion(getpid(), region, Value(value));

    std::size_t hits = 0;
    for (const auto& result : results) {
        if (result.address() == address + offset) {
            ++hits;
        }
    }
    EXPECT_EQ(hits, 1);
}

TEST(ScanProcessTest, findsPlantedValue) {
    auto buffer = std::make_unique<std::array<std::byte, 32>>();
    const std::uint64_t sentinel = 0x1122334455667788ULL;
    std::memcpy(buffer->data(), &sentinel, sizeof(sentinel));
    const auto address = reinterpret_cast<std::uintptr_t>(buffer->data());

    MemoryScanLevel level;
    level.memoryProtection = static_cast<std::uint8_t>(
        MemoryProtection::READ | MemoryProtection::WRITE);
    level.memoryRegionType = static_cast<std::uint8_t>(
        MemoryRegionType::HEAP | MemoryRegionType::ANONYMOUS);

    const auto results =
        MemoryScanner::scanProcess(getpid(), level, Value(sentinel));

    bool found = false;
    for (const auto& result : results) {
        if (result.address() == address) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

}  // namespace memseek
