#include <unistd.h>

#include <gtest/gtest.h>

#include <cstdint>

#include "memseek/memory_region.hpp"

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

}  // namespace memseek
