#include "memseek/memory/scan.hpp"

#include <gtest/gtest.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
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

    ASSERT_TRUE(results.has_value())
        << (results ? "" : formatError(results.error()));
    ASSERT_EQ(results->size(), 2);
    EXPECT_EQ(results->at(0).address(), mapping.address());
    EXPECT_EQ(results->at(1).address(), mapping.address() + 1);
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
    ASSERT_TRUE(less.has_value()) << (less ? "" : formatError(less.error()));
    ASSERT_EQ(less->size(), 1);
    EXPECT_EQ(less->front().address(), mapping.address());

    const auto greater = scanner.scanRegion(
        getpid(), region, Value(std::uint8_t{5}), ScanType::GREATER);
    ASSERT_TRUE(greater.has_value())
        << (greater ? "" : formatError(greater.error()));
    ASSERT_EQ(greater->size(), 1);
    EXPECT_EQ(greater->front().address(), mapping.address() + 2);
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
    ASSERT_TRUE(results.has_value())
        << (results ? "" : formatError(results.error()));

    std::size_t hits = 0;

    for (const auto& result : *results) {
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

    ASSERT_TRUE(results.has_value())
        << (results ? "" : formatError(results.error()));
    ASSERT_EQ(results->size(), 1);
    EXPECT_EQ(results->front().address(), mapping.address() + offset);
}

TEST(ScanRegionTest, ReturnsNoMatchForAbsentOrOversizedTarget) {
    AnonymousMapping mapping(4096);
    const auto region = makeAnonymousRegion(mapping, 4096);
    std::memset(mapping.bytes(), 0, region.size);

    MemoryScanner scanner(1);

    const auto absent = scanner.scanRegion(
        getpid(), region, Value(std::uint64_t{0x1122334455667788ULL}));
    ASSERT_TRUE(absent.has_value())
        << (absent ? "" : formatError(absent.error()));
    EXPECT_TRUE(absent->empty());
    const auto oversized = scanner.scanRegion(
        getpid(), region, Value(std::vector<std::byte>(8192)));
    ASSERT_TRUE(oversized.has_value())
        << (oversized ? "" : formatError(oversized.error()));
    EXPECT_TRUE(oversized->empty());
}

TEST(ScanRegionTest, ReportsMemoryReadFailure) {
    const MemoryRegion region{
        .id = 7,
        .start = 1,
        .size = 32,
        .protection = MemoryProtection::READ,
        .regionType = MemoryRegionType::ANONYMOUS,
    };
    MemoryScanner scanner(1);

    const auto result =
        scanner.scanRegion(getpid(), region, Value(std::uint8_t{1}));

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::MEMORY_READ_FAILED);
    EXPECT_TRUE(result.error().address.has_value());
}

TEST(ScanRegionTest, RejectsRegionAddressOverflowBeforeChunking) {
    const MemoryRegion region{
        .id = 7,
        .start = std::numeric_limits<std::uintptr_t>::max() - 7,
        .size = 16,
    };
    MemoryScanner scanner(1);

    const auto result =
        scanner.scanRegion(getpid(), region, Value(std::uint8_t{1}));

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::ADDRESS_OUT_OF_RANGE);
    EXPECT_EQ(result.error().address, region.start);
    EXPECT_EQ(result.error().size, region.size);
}

TEST(ScanRegionTest, ReportsShortReadInsteadOfReturningPartialMatches) {
    const auto pageSize = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
    AnonymousMapping mapping(pageSize * 2);
    ASSERT_EQ(::munmap(mapping.bytes() + pageSize, pageSize), 0);
    const auto region = makeAnonymousRegion(mapping, pageSize * 2);
    MemoryScanner scanner(1);

    const auto result =
        scanner.scanRegion(getpid(), region, Value(std::uint8_t{1}));

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::SHORT_TRANSFER);
    EXPECT_EQ(result.error().address, region.start);
    EXPECT_TRUE(result.error().bytesTransferred.has_value());
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
    ASSERT_TRUE(results.has_value())
        << (results ? "" : formatError(results.error()));

    const bool found =
        std::ranges::any_of(*results, [address](const ScanResult& result) {
            return result.address() == address;
        });

    EXPECT_TRUE(found);
}

TEST(ScanProcessTest, ReportsMappingEnumerationFailure) {
    MemoryScanner scanner(1);
    const auto result =
        scanner.scanProcess(-1, MemoryScanLevel{}, Value(std::uint8_t{1}));
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::MAPPING_QUERY_FAILED);
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
    ASSERT_TRUE(results.has_value())
        << (results ? "" : formatError(results.error()));

    EXPECT_TRUE(
        std::ranges::any_of(*results, [address](const ScanResult& result) {
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

    ASSERT_TRUE(one.has_value()) << (one ? "" : formatError(one.error()));
    ASSERT_TRUE(many.has_value()) << (many ? "" : formatError(many.error()));
    ASSERT_EQ(one->size(), 2);
    ASSERT_EQ(many->size(), one->size());
    ASSERT_EQ(one->at(0).address(), mapping.address() + firstOffset);
    ASSERT_EQ(one->at(1).address(), mapping.address() + secondOffset);
    EXPECT_EQ(*many, *one);
}

TEST(ScanRegionTest, ParallelScanFailsWhenAnyChunkIsShortRead) {
    const auto regionSize = 2 * DEFAULT_CHUNK_SIZE;
    AnonymousMapping mapping(regionSize);
    const auto pageSize = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
    ASSERT_EQ(::munmap(mapping.bytes() + regionSize - pageSize, pageSize), 0);
    const auto region = makeAnonymousRegion(mapping, regionSize);
    MemoryScanner scanner(4);

    const auto result =
        scanner.scanRegion(getpid(), region, Value(std::uint8_t{1}));

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::SHORT_TRANSFER);
    EXPECT_TRUE(result.error().address.has_value());
}

}  // namespace memseek
