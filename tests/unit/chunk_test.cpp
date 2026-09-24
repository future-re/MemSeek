#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "memseek/memory_chunk.hpp"

namespace memseek {
namespace {

auto collectChunks(const MemoryRegion& region,
                   const std::size_t chunkSize) -> std::vector<MemoryChunk> {
    std::vector<MemoryChunk> chunks;
    for (const auto chunk : MemoryChunkRange(region, chunkSize)) {
        chunks.push_back(chunk);
    }
    return chunks;
}

}  // namespace

TEST(MemoryChunkRangeTest, SplitsRegionAndPreservesTotalSize) {
    const MemoryRegion region{.id = 42, .start = 0x1000, .size = 10};

    const auto chunks = collectChunks(region, 4);

    ASSERT_EQ(chunks.size(), 3);
    EXPECT_EQ(chunks[0].address, 0x1000);
    EXPECT_EQ(chunks[0].size, 4);
    EXPECT_EQ(chunks[1].address, 0x1004);
    EXPECT_EQ(chunks[1].size, 4);
    EXPECT_EQ(chunks[2].address, 0x1008);
    EXPECT_EQ(chunks[2].size, 2);

    std::size_t totalSize = 0;
    for (const auto& chunk : chunks) {
        EXPECT_EQ(chunk.parentRegionId, 42);
        totalSize += chunk.size;
    }
    EXPECT_EQ(totalSize, region.size);
}

TEST(MemoryChunkRangeTest, HandlesEmptyAndExactSizeRegions) {
    const MemoryRegion empty{.id = 1, .start = 0x2000, .size = 0};
    EXPECT_TRUE(collectChunks(empty, 4).empty());

    const MemoryRegion exact{.id = 2, .start = 0x3000, .size = 8};
    const auto chunks = collectChunks(exact, 4);
    ASSERT_EQ(chunks.size(), 2);
    EXPECT_EQ(chunks[0].size, 4);
    EXPECT_EQ(chunks[1].size, 4);
}

TEST(MemoryChunkRangeTest, RejectsZeroChunkSize) {
    const MemoryRegion region{.start = 0x1000, .size = 1};

    EXPECT_THROW(MemoryChunkRange(region, 0), std::invalid_argument);
}

TEST(MemoryChunkRangeTest, SupportsSingleChunkRegions) {
    const MemoryRegion region{.id = 7, .start = 0x4000, .size = 3};

    const auto chunks = collectChunks(region, 8);

    ASSERT_EQ(chunks.size(), 1);
    EXPECT_EQ(chunks.front().address, region.start);
    EXPECT_EQ(chunks.front().size, region.size);
    EXPECT_EQ(chunks.front().parentRegionId, region.id);
}

}  // namespace memseek
