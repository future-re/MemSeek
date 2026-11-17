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
    ASSERT_EQ(results->results.size(), 2);
    EXPECT_TRUE(results->complete());
    EXPECT_EQ(results->results.at(0).address(), mapping.address());
    EXPECT_EQ(results->results.at(1).address(), mapping.address() + 1);
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
    ASSERT_EQ(less->results.size(), 1);
    EXPECT_TRUE(less->complete());
    EXPECT_EQ(less->results.front().address(), mapping.address());

    const auto greater = scanner.scanRegion(
        getpid(), region, Value(std::uint8_t{5}), ScanType::GREATER);
    ASSERT_TRUE(greater.has_value())
        << (greater ? "" : formatError(greater.error()));
    ASSERT_EQ(greater->results.size(), 1);
    EXPECT_TRUE(greater->complete());
    EXPECT_EQ(greater->results.front().address(), mapping.address() + 2);
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

    for (const auto& result : results->results) {
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
    ASSERT_EQ(results->results.size(), 1);
    EXPECT_EQ(results->results.front().address(), mapping.address() + offset);
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
    EXPECT_TRUE(absent->results.empty());
    EXPECT_TRUE(absent->complete());
    const auto oversized = scanner.scanRegion(
        getpid(), region, Value(std::vector<std::byte>(8192)));
    ASSERT_TRUE(oversized.has_value())
        << (oversized ? "" : formatError(oversized.error()));
    EXPECT_TRUE(oversized->results.empty());
    EXPECT_TRUE(oversized->complete());
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

    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->complete());
    EXPECT_TRUE(result->results.empty());
    ASSERT_EQ(result->issues.size(), 1);
    EXPECT_EQ(result->issues.front().address, region.start);
    EXPECT_EQ(result->issues.front().size, region.size);
    EXPECT_EQ(result->issues.front().error.code, ErrorCode::MEMORY_READ_FAILED);
    EXPECT_EQ(result->issues.front().error.pid, getpid());
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
    MemoryScanner scanner(1);
    ASSERT_EQ(::munmap(mapping.bytes() + pageSize, pageSize), 0);
    const auto region = makeAnonymousRegion(mapping, pageSize * 2);

    const auto result =
        scanner.scanRegion(getpid(), region, Value(std::uint8_t{1}));

    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->complete());
    EXPECT_TRUE(result->results.empty());
    ASSERT_EQ(result->issues.size(), 1);
    const auto& issue = result->issues.front();
    EXPECT_EQ(issue.address, region.start);
    EXPECT_EQ(issue.size, region.size);
    EXPECT_EQ(issue.error.code, ErrorCode::SHORT_TRANSFER);
    EXPECT_EQ(issue.error.bytesTransferred, pageSize);
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

    const bool found = std::ranges::any_of(
        results->results, [address](const ScanResult& result) {
            return result.address() == address;
        });

    EXPECT_TRUE(found);
}

TEST(ScanProcessTest, ReportsMappingEnumerationFailure) {
    MemoryScanner scanner(1);
    for (const auto strategy :
         {ScanStrategy::STRICT, ScanStrategy::TOLERANCE}) {
        MemoryScanLevel level;
        level.memoryScanStrategy = strategy;
        const auto result =
            scanner.scanProcess(-1, level, Value(std::uint8_t{1}));
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().code, ErrorCode::MAPPING_QUERY_FAILED);
    }
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

    EXPECT_TRUE(std::ranges::any_of(results->results,
                                    [address](const ScanResult& result) {
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
    EXPECT_TRUE(one->complete());
    EXPECT_TRUE(many->complete());
    ASSERT_EQ(one->results.size(), 2);
    ASSERT_EQ(many->results.size(), one->results.size());
    ASSERT_EQ(one->results.at(0).address(), mapping.address() + firstOffset);
    ASSERT_EQ(one->results.at(1).address(), mapping.address() + secondOffset);
    EXPECT_EQ(many->results, one->results);
}

TEST(ScanRegionTest, ParallelScanRetainsMatchesWhenAnotherChunkIsShortRead) {
    const auto regionSize = 2 * DEFAULT_CHUNK_SIZE;
    AnonymousMapping mapping(regionSize);
    mapping.setByte(16, std::byte{123});
    MemoryScanner scanner(4);
    const auto pageSize = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
    ASSERT_EQ(::munmap(mapping.bytes() + regionSize - pageSize, pageSize), 0);
    const auto region = makeAnonymousRegion(mapping, regionSize);

    const auto result =
        scanner.scanRegion(getpid(), region, Value(std::uint8_t{123}));

    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->complete());
    ASSERT_EQ(result->results.size(), 1);
    EXPECT_EQ(result->results.front().address(), mapping.address() + 16);
    ASSERT_EQ(result->issues.size(), 1);
    const auto& issue = result->issues.front();
    EXPECT_EQ(issue.address, region.start + DEFAULT_CHUNK_SIZE);
    EXPECT_EQ(issue.size, DEFAULT_CHUNK_SIZE);
    EXPECT_EQ(issue.error.code, ErrorCode::SHORT_TRANSFER);
    EXPECT_EQ(issue.error.bytesTransferred, DEFAULT_CHUNK_SIZE - pageSize);
}

TEST(ScanProcessTest, ToleranceKeepsMatchesFromCompleteRegions) {
    constexpr std::uint64_t sentinel = 0x123456789abcdef0ULL;
    AnonymousMapping mapping(4096);
    std::memcpy(mapping.bytes(), &sentinel, sizeof(sentinel));
    MemoryScanLevel level;
    level.memoryRegionType =
        static_cast<std::uint8_t>(MemoryRegionType::ANONYMOUS);
    level.memoryScanStrategy = ScanStrategy::TOLERANCE;
    MemoryScanner scanner(1);

    const auto report = scanner.scanProcess(getpid(), level, Value(sentinel));

    ASSERT_TRUE(report.has_value())
        << (report ? "" : formatError(report.error()));
    EXPECT_TRUE(std::ranges::any_of(
        report->results, [&mapping](const ScanResult& result) {
            return result.address() == mapping.address();
        }));
}

TEST(ScanProcessTest, RejectsInvalidStrategy) {
    MemoryScanLevel level;
    level.memoryScanStrategy = static_cast<ScanStrategy>(255);
    MemoryScanner scanner(1);

    const auto report =
        scanner.scanProcess(getpid(), level, Value(std::uint8_t{1}));

    ASSERT_FALSE(report.has_value());
    EXPECT_EQ(report.error().code, ErrorCode::INVALID_ARGUMENT);
}

}  // namespace memseek
