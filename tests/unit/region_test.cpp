#include <unistd.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>

#include "memseek/memory_region.hpp"
#include "memseek/memory_scan.hpp"
#include "memseek/value.hpp"

namespace memseek {
namespace {

std::uint8_t protectionBits(const MemoryRegion& region) {
    return static_cast<std::uint8_t>(region.protection);
}

}  // namespace

TEST(RegionTest, readProcessFiltersByProtection) {
    MemoryScanLevel level;
    level.memoryProtection = static_cast<std::uint8_t>(
        MemoryProtection::READ | MemoryProtection::WRITE);

    auto regions = readProcess(getpid(), level);
    ASSERT_TRUE(regions.has_value());

    for (const auto& region : regions->getRegions()) {
        const auto bits = protectionBits(region);
        EXPECT_NE(bits & static_cast<std::uint8_t>(MemoryProtection::READ), 0);
        EXPECT_NE(bits & static_cast<std::uint8_t>(MemoryProtection::WRITE), 0);
    }
}

TEST(RegionTest, readProcessFiltersByRegionType) {
    MemoryScanLevel level;
    level.memoryProtection = static_cast<std::uint8_t>(MemoryProtection::READ);
    level.memoryRegionType = static_cast<std::uint8_t>(MemoryRegionType::HEAP);

    auto regions = readProcess(getpid(), level);
    ASSERT_TRUE(regions.has_value());

    for (const auto& region : regions->getRegions()) {
        EXPECT_EQ(region.regionType, MemoryRegionType::HEAP);
    }
}

TEST(ScanTest, processScanFindsPlantedValue) {
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
