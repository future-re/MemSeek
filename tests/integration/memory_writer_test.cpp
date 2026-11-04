#include <gtest/gtest.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

#include "memseek/memory/region.hpp"
#include "memseek/memory/writer.hpp"

namespace memseek {
namespace {

class WritableMapping {
   public:
    explicit WritableMapping(const std::size_t size)
        : m_address(::mmap(nullptr, size, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)),
          m_size(size) {
        if (m_address == MAP_FAILED) {
            throw std::runtime_error("mmap failed");
        }
    }

    ~WritableMapping() {
        if (m_address != MAP_FAILED) {
            ::munmap(m_address, m_size);
        }
    }

    WritableMapping(const WritableMapping&) = delete;
    WritableMapping& operator=(const WritableMapping&) = delete;

    [[nodiscard]] auto address() const noexcept -> std::uintptr_t {
        return reinterpret_cast<std::uintptr_t>(m_address);
    }

    [[nodiscard]] auto data() const noexcept -> const std::byte* {
        return static_cast<const std::byte*>(m_address);
    }

   private:
    void* m_address{MAP_FAILED};
    std::size_t m_size{};
};

auto writableMappings(const pid_t pid) -> Result<MemoryRegionList> {
    MemoryScanLevel level;
    level.memoryProtection = static_cast<std::uint8_t>(MemoryProtection::WRITE);
    level.memoryRegionType =
        static_cast<std::uint8_t>(MemoryRegionType::HEAP) |
        static_cast<std::uint8_t>(MemoryRegionType::STACK) |
        static_cast<std::uint8_t>(MemoryRegionType::ANONYMOUS) |
        static_cast<std::uint8_t>(MemoryRegionType::MODULE);
    return readProcess(pid, level, false);
}

auto findMapping(const MemoryRegionList& regions, const std::uintptr_t address)
    -> const MemoryRegion* {
    for (const auto& region : regions.getRegions()) {
        if (address >= region.start && address - region.start < region.size) {
            return &region;
        }
    }
    return nullptr;
}

}  // namespace

TEST(MemoryWriterTest, WritesWithinWritableMapping) {
    WritableMapping mapping(4096);
    const auto regions = writableMappings(getpid());
    ASSERT_TRUE(regions.has_value());
    const auto* region = findMapping(*regions, mapping.address());
    ASSERT_NE(region, nullptr);

    constexpr std::array value{std::byte{0x12}, std::byte{0x34},
                               std::byte{0x56}, std::byte{0x78}};
    MemoryWriter writer(getpid());
    const auto result = writer.write({.address = mapping.address() + 32,
                                      .size = value.size(),
                                      .parentRegionId = region->id},
                                     value);

    ASSERT_TRUE(result.has_value())
        << (result ? "" : formatError(result.error()));
    EXPECT_EQ(*result, value.size());
    EXPECT_TRUE(std::equal(value.begin(), value.end(), mapping.data() + 32));
}

TEST(MemoryWriterTest, RejectsRangeOutsideWritableMapping) {
    MemoryWriter writer(getpid());
    constexpr std::array value{std::byte{0x42}};

    const auto result =
        writer.write({.address = 1, .size = value.size()}, value);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::MAPPING_NOT_WRITABLE);
    EXPECT_EQ(result.error().bytesTransferred, 0);
    EXPECT_NE(result.error().message.find("not contained"), std::string::npos);
}

TEST(MemoryWriterTest, RejectsMismatchedParentRegionId) {
    WritableMapping mapping(4096);
    const auto regions = writableMappings(getpid());
    ASSERT_TRUE(regions.has_value());
    const auto* region = findMapping(*regions, mapping.address());
    ASSERT_NE(region, nullptr);
    MemoryWriter writer(getpid());
    constexpr std::array value{std::byte{0x42}};

    const auto result = writer.write({.address = mapping.address(),
                                      .size = value.size(),
                                      .parentRegionId = region->id + 1},
                                     value);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::REGION_MISMATCH);
    EXPECT_EQ(result.error().bytesTransferred, 0);
    EXPECT_NE(result.error().message.find("region id"), std::string::npos);
}

TEST(MemoryWriterTest, ReportsDataLargerThanTarget) {
    MemoryWriter writer(getpid());
    constexpr std::array value{std::byte{0x12}, std::byte{0x34}};

    const auto result = writer.write({.address = 1, .size = 1}, value);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::INVALID_ARGUMENT);
    EXPECT_EQ(result.error().bytesTransferred, 0);
    EXPECT_NE(result.error().message.find("exceeds target size"),
              std::string::npos);
}

TEST(MemoryWriterTest, RejectsReadOnlyMappingBeforeWriting) {
    WritableMapping mapping(4096);
    ASSERT_EQ(
        ::mprotect(const_cast<std::byte*>(mapping.data()), 4096, PROT_READ), 0);
    MemoryWriter writer(getpid());
    constexpr std::array value{std::byte{0x42}};

    const auto result = writer.write(
        {.address = mapping.address(), .size = value.size()}, value);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::MAPPING_NOT_WRITABLE);
    EXPECT_EQ(result.error().bytesTransferred, 0);
    EXPECT_NE(result.error().message.find("not contained"), std::string::npos);
}

}  // namespace memseek
