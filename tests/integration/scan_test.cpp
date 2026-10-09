#include "memseek/memory/scan.hpp"

#include <gtest/gtest.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

#include "memseek/memory/chunk.hpp"
#include "memseek/memory/region.hpp"
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

auto makeAnonymousRegion(const AnonymousMapping& mapping, std::size_t size,
                         std::uint64_t regionId = 1) -> MemoryRegion {
    return {.id = regionId,
            .start = mapping.address(),
            .size = size,
            .protection = MemoryProtection::READ | MemoryProtection::WRITE,
            .regionType = MemoryRegionType::ANONYMOUS};
}

}  // namespace

TEST(ScanRegionTest, SupportsNumericRangeScan) {
    AnonymousMapping mapping(4096);
    const auto region = makeAnonymousRegion(mapping, 3);

    mapping.setByte(0, std::byte{1});
    mapping.setByte(1, std::byte{5});
    mapping.setByte(2, std::byte{9});

    MemoryScanner scanner(1);
    const auto results = scanner.scanRegion(
        getpid(), region, Value(std::uint8_t{1}), Value(std::uint8_t{5}));

    ASSERT_EQ(results.size(), 2);
    EXPECT_EQ(results.at(0).address(), mapping.address());
    EXPECT_EQ(results.at(1).address(), mapping.address() + 1);
}

TEST(ScanRegionTest, SupportsGreaterAndLessScans) {
    AnonymousMapping mapping(4096);
    const auto region = makeAnonymousRegion(mapping, 3);

    mapping.setByte(0, std::byte{1});
    mapping.setByte(1, std::byte{5});
    mapping.setByte(2, std::byte{9});

    MemoryScanner scanner(1);

    const auto less = scanner.scanRegion(
        getpid(), region, Value(std::uint8_t{5}), ScanType::LESS);
    ASSERT_EQ(less.size(), 1);
    EXPECT_EQ(less.front().address(), mapping.address());

    const auto greater = scanner.scanRegion(
        getpid(), region, Value(std::uint8_t{5}), ScanType::GREATER);
    ASSERT_EQ(greater.size(), 1);
    EXPECT_EQ(greater.front().address(), mapping.address() + 2);
}

TEST(ScanRegionTest, FindsValueSpanningChunkBoundary) {
    constexpr std::uint32_t value = 0x11223344;

    AnonymousMapping mapping(2 * DEFAULT_CHUNK_SIZE);
    const auto region = makeAnonymousRegion(mapping, 2 * DEFAULT_CHUNK_SIZE);

    const std::size_t offset = DEFAULT_CHUNK_SIZE - (sizeof(value) / 2);

    std::memcpy(mapping.bytes() + offset, &value, sizeof(value));
    const auto address = mapping.address();

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

TEST(ScanRegionTest, FindsMatchAtTheEndOfTheRegion) {
    constexpr std::uint32_t value = 0x55667788;
    AnonymousMapping mapping(4096);
    const auto region = makeAnonymousRegion(mapping, 4096);

    const auto offset = region.size - sizeof(value);
    std::memcpy(mapping.bytes() + offset, &value, sizeof(value));

    MemoryScanner scanner(1);
    const auto results = scanner.scanRegion(getpid(), region, Value(value));

    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results.front().address(), mapping.address() + offset);
}

TEST(ScanRegionTest, ReturnsNoMatchForAbsentOrOversizedTarget) {
    AnonymousMapping mapping(4096);
    const auto region = makeAnonymousRegion(mapping, 4096);
    std::memset(mapping.bytes(), 0, region.size);

    MemoryScanner scanner(1);

    EXPECT_TRUE(scanner
                    .scanRegion(getpid(), region,
                                Value(std::uint64_t{0x1122334455667788ULL}))
                    .empty());
    EXPECT_TRUE(
        scanner
            .scanRegion(getpid(), region, Value(std::vector<std::byte>(8192)))
            .empty());
}

TEST(ScanProcessTest, FindsPlantedValue) {
    constexpr std::uint64_t sentinel = 0x1122334455667788ULL;
    AnonymousMapping mapping(4096);

    std::memcpy(mapping.bytes(), &sentinel, sizeof(sentinel));
    const auto address = mapping.address();

    MemoryScanLevel level;

    level.memoryProtection = static_cast<std::uint8_t>(MemoryProtection::READ |
                                                       MemoryProtection::WRITE);

    level.memoryRegionType = static_cast<std::uint8_t>(
        MemoryRegionType::HEAP | MemoryRegionType::ANONYMOUS);

    MemoryScanner scanner;

    const auto results = scanner.scanProcess(getpid(), level, Value(sentinel));

    const bool found =
        std::ranges::any_of(results, [address](const ScanResult& result) {
            return result.address() == address;
        });

    EXPECT_TRUE(found);
}

TEST(ScanProcessTest, FindsPlantedValueWithRange) {
    AnonymousMapping mapping(4096);
    mapping.setByte(0, std::byte{9});
    const auto address = mapping.address();

    MemoryScanLevel level;
    level.memoryProtection = static_cast<std::uint8_t>(MemoryProtection::READ |
                                                       MemoryProtection::WRITE);
    level.memoryRegionType = static_cast<std::uint8_t>(
        MemoryRegionType::HEAP | MemoryRegionType::ANONYMOUS);

    MemoryScanner scanner;
    const auto results = scanner.scanProcess(
        getpid(), level, Value(std::uint8_t{8}), Value(std::uint8_t{10}));

    EXPECT_TRUE(
        std::ranges::any_of(results, [address](const ScanResult& result) {
            return result.address() == address;
        }));
}

TEST(ScanProcessTest, ReturnsMatchesInDeterministicOrderWithDifferentWorkers) {
    constexpr std::uint32_t value = 0xaabbccdd;
    AnonymousMapping mapping(2 * DEFAULT_CHUNK_SIZE);
    const auto region = makeAnonymousRegion(mapping, 2 * DEFAULT_CHUNK_SIZE);

    const auto firstOffset = DEFAULT_CHUNK_SIZE - 1;
    const auto secondOffset = DEFAULT_CHUNK_SIZE + 32;
    std::memcpy(mapping.bytes() + firstOffset, &value, sizeof(value));
    std::memcpy(mapping.bytes() + secondOffset, &value, sizeof(value));

    MemoryScanner singleWorker(1);
    MemoryScanner manyWorkers(4);
    const auto target = Value(value);

    const auto one = singleWorker.scanRegion(getpid(), region, target);
    const auto many = manyWorkers.scanRegion(getpid(), region, target);

    ASSERT_EQ(one.size(), 2);
    ASSERT_EQ(many.size(), one.size());
    ASSERT_EQ(one.at(0).address(), mapping.address() + firstOffset);
    ASSERT_EQ(one.at(1).address(), mapping.address() + secondOffset);
    EXPECT_EQ(many, one);
}

}  // namespace memseek
