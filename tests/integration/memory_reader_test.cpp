#include "memseek/memory_reader.hpp"

#include <gtest/gtest.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace memseek {
namespace {

class AnonymousMapping {
   public:
    explicit AnonymousMapping(std::size_t size) : m_size(size) {
        m_address = ::mmap(nullptr, size, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (m_address == MAP_FAILED) {
            throw std::runtime_error("mmap failed");
        }
    }

    ~AnonymousMapping() {
        if (m_address != MAP_FAILED) ::munmap(m_address, m_size);
    }

    AnonymousMapping(const AnonymousMapping&) = delete;
    AnonymousMapping& operator=(const AnonymousMapping&) = delete;

    [[nodiscard]] auto address() const noexcept -> std::uintptr_t {
        return reinterpret_cast<std::uintptr_t>(m_address);
    }

    [[nodiscard]] auto bytes() noexcept -> std::byte* {
        return static_cast<std::byte*>(m_address);
    }

   private:
    void* m_address{MAP_FAILED};
    std::size_t m_size{};
};

auto makeRegion(const AnonymousMapping& mapping,
                std::size_t size) -> MemoryRegion {
    return {.id = 1,
            .start = mapping.address(),
            .size = size,
            .protection = MemoryProtection::READ | MemoryProtection::WRITE,
            .regionType = MemoryRegionType::ANONYMOUS};
}

}  // namespace

TEST(MemoryReaderTest, ReadsChunkAndIncludesTargetOverlap) {
    AnonymousMapping mapping(4096);
    constexpr std::uint32_t value = 0x11223344;
    std::memcpy(mapping.bytes() + 16, &value, sizeof(value));

    const auto region = makeRegion(mapping, 4096);
    const MemoryChunk chunk{.address = mapping.address() + 16,
                            .size = 8,
                            .parentRegionId = region.id};
    const MemoryReader reader(getpid());

    const auto result = reader.read(chunk, region, Value(value));

    if (!result) FAIL() << result.error();
    EXPECT_EQ(result->address(), chunk.address);
    EXPECT_EQ(result->logicalSize(), chunk.size);
    EXPECT_EQ(result->size(), chunk.size + sizeof(value) - 1);
    EXPECT_TRUE(std::ranges::equal(result->data().subspan(0, sizeof(value)),
                                   Value(value).data()));
}

TEST(MemoryReaderTest, ClampsOverlapAtRegionEnd) {
    AnonymousMapping mapping(4096);
    constexpr std::uint64_t value = 0x0102030405060708ULL;
    std::memcpy(mapping.bytes() + 4080, &value, sizeof(value));

    const auto region = makeRegion(mapping, 4096);
    const MemoryChunk chunk{.address = mapping.address() + 4080,
                            .size = 16,
                            .parentRegionId = region.id};
    const MemoryReader reader(getpid());

    const auto result = reader.read(chunk, region, Value(value));

    if (!result) FAIL() << result.error();
    EXPECT_EQ(result->size(), 16);
}

TEST(MemoryReaderTest, RejectsEmptyTarget) {
    AnonymousMapping mapping(4096);
    const auto region = makeRegion(mapping, 4096);
    const MemoryChunk chunk{
        .address = mapping.address(), .size = 16, .parentRegionId = region.id};
    const MemoryReader reader(getpid());

    const auto result =
        reader.read(chunk, region, Value(std::vector<std::byte>{}));

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), "target size must be greater than zero");
}

TEST(MemoryReaderTest, ReportsInvalidProcess) {
    AnonymousMapping mapping(4096);
    const auto region = makeRegion(mapping, 4096);
    const MemoryChunk chunk{
        .address = mapping.address(), .size = 16, .parentRegionId = region.id};
    const MemoryReader reader(-1);

    const auto result = reader.read(chunk, region, Value(std::uint32_t{1}));

    EXPECT_FALSE(result.has_value());
}

TEST(MemoryReaderTest, RejectsChunkOutsideRegion) {
    AnonymousMapping mapping(4096);
    const auto region = makeRegion(mapping, 4096);
    const MemoryChunk chunk{.address = mapping.address() + 4090,
                            .size = 16,
                            .parentRegionId = region.id};
    const MemoryReader reader(getpid());

    const auto result = reader.read(chunk, region, Value(std::uint32_t{1}));

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), "memory chunk is outside the region");
}

}  // namespace memseek
