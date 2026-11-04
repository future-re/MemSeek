#include <gtest/gtest.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <stdexcept>

#include "memseek/memory/reader.hpp"
#include "memseek/value.hpp"

namespace memseek {
namespace {

class AnonymousMapping {
   public:
    explicit AnonymousMapping(std::size_t size)
        : m_address(::mmap(nullptr, size, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)),
          m_size(size) {
        if (m_address == MAP_FAILED) {
            throw std::runtime_error("mmap failed");
        }
    }

    ~AnonymousMapping() {
        if (m_address != MAP_FAILED) {
            ::munmap(m_address, m_size);
        }
    }

    AnonymousMapping(const AnonymousMapping&) = delete;
    AnonymousMapping& operator=(const AnonymousMapping&) = delete;
    AnonymousMapping(AnonymousMapping&&) = delete;
    AnonymousMapping& operator=(AnonymousMapping&&) = delete;

    [[nodiscard]] auto address() const noexcept -> std::uintptr_t {
        return reinterpret_cast<std::uintptr_t>(m_address);
    }

    [[nodiscard]] auto bytes() noexcept -> std::byte* {
        return static_cast<std::byte*>(m_address);
    }

    void setByte(const std::size_t offset, const std::byte value) {
        if (offset >= m_size) {
            throw std::out_of_range("anonymous mapping offset out of range");
        }
        *(static_cast<std::byte*>(m_address) + offset) = value;
    }

   private:
    void* m_address{MAP_FAILED};
    std::size_t m_size{};
};

}  // namespace

TEST(MemoryReaderTest, ReadsExactAddressRange) {
    AnonymousMapping mapping(4096);
    constexpr std::uint32_t value = 0x11223344;
    std::memcpy(mapping.bytes() + 16, &value, sizeof(value));

    const MemoryReader reader(getpid());

    const auto result = reader.read(mapping.address() + 16, 8);

    if (!result) {
        FAIL() << formatError(result.error());
    }
    EXPECT_EQ(result->size(), 8);
    EXPECT_TRUE(std::ranges::equal(std::span(*result).first(sizeof(value)),
                                   Value(value).data()));
}

TEST(MemoryReaderTest, ReadsRangeEndingAtMappingBoundary) {
    AnonymousMapping mapping(4096);
    constexpr std::uint64_t value = 0x0102030405060708ULL;
    std::memcpy(mapping.bytes() + 4080, &value, sizeof(value));

    const MemoryReader reader(getpid());

    const auto result = reader.read(mapping.address() + 4080, 16);

    if (!result) {
        FAIL() << formatError(result.error());
    }
    EXPECT_EQ(result->size(), 16);
    EXPECT_TRUE(std::ranges::equal(std::span(*result).first(sizeof(value)),
                                   Value(value).data()));
}

TEST(MemoryReaderTest, ReportsInvalidProcess) {
    AnonymousMapping mapping(4096);
    const MemoryReader reader(-1);

    const auto result = reader.read(mapping.address(), 16);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::MEMORY_READ_FAILED);
}

TEST(MemoryReaderTest, RejectsAddressRangeOverflow) {
    const MemoryReader reader(getpid());
    const auto address = std::numeric_limits<std::uintptr_t>::max() - 7;
    const auto result = reader.read(address, 16);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::ADDRESS_OUT_OF_RANGE);
    EXPECT_EQ(result.error().address, address);
    EXPECT_EQ(result.error().size, 16);
}

TEST(MemoryReaderTest, EmptyReadDoesNotAccessProcess) {
    const MemoryReader reader(-1);
    const auto result = reader.read(0, 0);

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->empty());
}

TEST(MemoryReaderTest, ReportsShortReadWithoutReturningPartialData) {
    const auto pageSize = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
    AnonymousMapping mapping(pageSize * 2);
    ASSERT_EQ(::munmap(mapping.bytes() + pageSize, pageSize), 0);
    const MemoryReader reader(getpid());

    const auto result = reader.read(mapping.address(), pageSize * 2);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::SHORT_TRANSFER);
    EXPECT_EQ(result.error().address, mapping.address());
    EXPECT_EQ(result.error().size, pageSize * 2);
    EXPECT_EQ(result.error().bytesTransferred, pageSize);
}

}  // namespace memseek
