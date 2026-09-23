#include <gtest/gtest.h>
#include <unistd.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
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
    constexpr std::uintptr_t address = 0x1000;

    std::vector<std::byte> data{
        std::byte{0x12}, std::byte{0x34}, std::byte{0x56},
        std::byte{0x78}, std::byte{0x99},
    };

    MemoryRead memory(address, data.size(), std::move(data));

    const Value target(static_cast<std::uint16_t>(0x7856));

    const auto results = MemoryScanner::scanBuffer(memory, target);

    ASSERT_EQ(results.size(), 1);

    EXPECT_EQ(results[0].address(), address + 2);
    EXPECT_EQ(results[0].data(), target);
}

TEST(ScanBufferTest, multipleValueScan) {
    constexpr std::uintptr_t address = 0x2000;

    std::vector<std::byte> data;

    data += changeValue(std::uint32_t{45});
    data += changeValue(std::uint32_t{36});
    data += changeValue(std::uint32_t{45});

    MemoryRead memory(address, data.size(), std::move(data));

    const Value target45(std::uint32_t{45});

    const auto results = MemoryScanner::scanBuffer(memory, target45);

    ASSERT_EQ(results.size(), 2);

    EXPECT_EQ(results[0].data(), target45);
    EXPECT_EQ(results[1].data(), target45);

    const Value target36(std::uint32_t{36});

    EXPECT_EQ(MemoryScanner::scanBuffer(memory, target36).size(), 1);
}

TEST(ScanBufferTest, stringScan) {
    constexpr std::uintptr_t address = 0x2000;

    std::vector<std::byte> data{
        std::byte{'H'}, std::byte{'e'}, std::byte{'l'}, std::byte{'l'},
        std::byte{'o'}, std::byte{' '}, std::byte{'W'}, std::byte{'o'},
        std::byte{'r'}, std::byte{'l'}, std::byte{'d'},
    };

    MemoryRead memory(address, data.size(), std::move(data));

    const Value target(std::string{"lo Wo"});

    const auto results = MemoryScanner::scanBuffer(memory, target);

    ASSERT_EQ(results.size(), 1);

    EXPECT_EQ(results[0].address(), address + 3);
    EXPECT_EQ(results[0].data(), target);
}

TEST(ScanRegionTest, findsValueSpanningChunkBoundary) {
    constexpr std::uint32_t value = 0x11223344;

    std::vector<std::byte> buffer(2 * DEFAULT_CHUNK_SIZE, std::byte{0});

    const std::size_t offset = DEFAULT_CHUNK_SIZE - sizeof(value) / 2;

    std::memcpy(buffer.data() + offset, &value, sizeof(value));

    const auto address = reinterpret_cast<std::uintptr_t>(buffer.data());

    const auto region = findRegion(address);

    ASSERT_NE(region.size, 0);

    MemoryScanner scanner;

    const auto results = scanner.scanRegion(getpid(), region, Value(value));

    std::size_t hits = 0;

    for (const auto& result : results) {
        if (result.address() == address + offset) {
            ++hits;
        }
    }

    //
    // overlap read must find the boundary-spanning value,
    // but it must not be reported twice.
    //
    EXPECT_EQ(hits, 1);
}

TEST(ScanProcessTest, findsPlantedValue) {
    auto buffer = std::make_unique<std::array<std::byte, 32>>();

    constexpr std::uint64_t sentinel = 0x1122334455667788ULL;

    std::memcpy(buffer->data(), &sentinel, sizeof(sentinel));

    const auto address = reinterpret_cast<std::uintptr_t>(buffer->data());

    MemoryScanLevel level;

    level.memoryProtection = static_cast<std::uint8_t>(MemoryProtection::READ |
                                                       MemoryProtection::WRITE);

    level.memoryRegionType = static_cast<std::uint8_t>(
        MemoryRegionType::HEAP | MemoryRegionType::ANONYMOUS);

    MemoryScanner scanner;

    const auto results = scanner.scanProcess(getpid(), level, Value(sentinel));

    const bool found = std::any_of(results.begin(), results.end(),
                                   [address](const ScanResult& result) {
                                       return result.address() == address;
                                   });

    EXPECT_TRUE(found);
}

}  // namespace memseek