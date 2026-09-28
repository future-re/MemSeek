#include <gtest/gtest.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
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
        if (m_address != MAP_FAILED) {
            ::munmap(m_address, m_size);
        }
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

auto makeAnonymousRegion(const AnonymousMapping& mapping, std::size_t size,
                         std::uint64_t id = 1) -> MemoryRegion {
    return {.id = id,
            .start = mapping.address(),
            .size = size,
            .protection = MemoryProtection::READ | MemoryProtection::WRITE,
            .regionType = MemoryRegionType::ANONYMOUS};
}

template <ValueNumericType T>
void expectNumericOrdering(T lower, T target, T upper) {
    const auto scan = [](T memoryValue, const Value& bound, ScanType scanType) {
        auto data = changeValue(memoryValue);
        const MemoryRead memory(0x6000, data.size(), std::move(data));
        return MemoryScanner::scanBuffer(memory, bound, scanType);
    };

    const auto scanRange = [](T memoryValue, const Value& lowerBound,
                              const Value& upperBound) {
        auto data = changeValue(memoryValue);
        const MemoryRead memory(0x6000, data.size(), std::move(data));
        return MemoryScanner::scanBuffer(memory, lowerBound, upperBound);
    };

    EXPECT_EQ(scan(lower, Value(target), ScanType::LESS).size(), 1);
    EXPECT_TRUE(scan(lower, Value(target), ScanType::GREATER).empty());
    EXPECT_EQ(scan(upper, Value(target), ScanType::GREATER).size(), 1);
    EXPECT_TRUE(scan(upper, Value(target), ScanType::LESS).empty());

    EXPECT_EQ(scanRange(target, Value(lower), Value(upper)).size(), 1);
    EXPECT_TRUE(scanRange(upper, Value(lower), Value(target)).empty());
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

TEST(ScanBufferTest, ReturnsNoMatchesForEmptyOrOversizedTargets) {
    const std::vector<std::byte> data{std::byte{0x01}, std::byte{0x02}};
    const MemoryRead memory(0x2000, data.size(), data);

    EXPECT_TRUE(
        MemoryScanner::scanBuffer(memory, Value(std::vector<std::byte>{}))
            .empty());
    EXPECT_TRUE(
        MemoryScanner::scanBuffer(
            memory, Value(std::vector<std::byte>{
                        std::byte{0x01}, std::byte{0x02}, std::byte{0x03}}))
            .empty());
}

TEST(ScanBufferTest, SupportsNumericScanTypes) {
    std::vector<std::byte> data;
    data += changeValue(std::uint8_t{1});
    data += changeValue(std::uint8_t{5});
    data += changeValue(std::uint8_t{9});

    constexpr std::uintptr_t address = 0x3000;
    MemoryRead memory(address, data.size(), std::move(data));

    const auto less = MemoryScanner::scanBuffer(memory, Value(std::uint8_t{5}),
                                                ScanType::LESS);
    ASSERT_EQ(less.size(), 1);
    EXPECT_EQ(less.front().address(), address);

    const auto greater = MemoryScanner::scanBuffer(
        memory, Value(std::uint8_t{5}), ScanType::GREATER);
    ASSERT_EQ(greater.size(), 1);
    EXPECT_EQ(greater.front().address(), address + 2);

    const auto range = MemoryScanner::scanBuffer(memory, Value(std::uint8_t{1}),
                                                 Value(std::uint8_t{5}));
    ASSERT_EQ(range.size(), 2);
    EXPECT_EQ(range[0].address(), address);
    EXPECT_EQ(range[1].address(), address + 1);
}

TEST(ScanBufferTest, SupportsOrderingForEveryNumericValueType) {
    expectNumericOrdering<std::uint8_t>(1, 5, 9);
    expectNumericOrdering<std::uint16_t>(1, 5, 9);
    expectNumericOrdering<std::uint32_t>(1, 5, 9);
    expectNumericOrdering<std::uint64_t>(1, 5, 9);
    expectNumericOrdering<std::int8_t>(-9, -2, 5);
    expectNumericOrdering<std::int16_t>(-9, -2, 5);
    expectNumericOrdering<std::int32_t>(-9, -2, 5);
    expectNumericOrdering<std::int64_t>(-9, -2, 5);
    expectNumericOrdering<float>(-1.5F, 0.25F, 3.5F);
    expectNumericOrdering<double>(-1.5, 0.25, 3.5);
}

TEST(ScanBufferTest, OrderingScansRejectNonNumericValues) {
    const std::vector<std::byte> data{std::byte{'a'}, std::byte{'b'},
                                      std::byte{'c'}};
    const MemoryRead memory(0x4000, data.size(), data);

    EXPECT_TRUE(MemoryScanner::scanBuffer(memory, Value(std::string{"b"}),
                                          ScanType::GREATER)
                    .empty());
    EXPECT_TRUE(MemoryScanner::scanBuffer(
                    memory, Value(std::vector<std::byte>{std::byte{'a'}}),
                    ScanType::LESS)
                    .empty());
    EXPECT_TRUE(MemoryScanner::scanBuffer(memory, Value(std::string{"a"}),
                                          Value(std::string{"z"}))
                    .empty());
}

TEST(ScanBufferTest, RejectsInvalidNumericRanges) {
    const auto data = changeValue(std::uint32_t{5});
    const MemoryRead memory(0x5000, data.size(), data);

    EXPECT_TRUE(MemoryScanner::scanBuffer(memory, Value(std::uint32_t{9}),
                                          Value(std::uint32_t{1}))
                    .empty());
    EXPECT_TRUE(MemoryScanner::scanBuffer(memory, Value(std::uint16_t{1}),
                                          Value(std::uint32_t{9}))
                    .empty());
    EXPECT_TRUE(
        MemoryScanner::scanBuffer(
            memory, Value(std::numeric_limits<float>::quiet_NaN()), Value(9.0F))
            .empty());
}

TEST(ScanBufferTest, OrderingScansDoNotMatchNaN) {
    const auto data = changeValue(std::numeric_limits<float>::quiet_NaN());
    const MemoryRead memory(0x5000, data.size(), data);

    EXPECT_TRUE(
        MemoryScanner::scanBuffer(memory, Value(1.0F), ScanType::GREATER)
            .empty());
    EXPECT_TRUE(
        MemoryScanner::scanBuffer(memory, Value(1.0F), ScanType::LESS).empty());
    EXPECT_TRUE(
        MemoryScanner::scanBuffer(memory, Value(0.0F), Value(2.0F)).empty());
}

TEST(ScanBufferTest, UnknownScanTypeReturnsNoMatches) {
    const auto data = changeValue(std::uint8_t{5});
    const MemoryRead memory(0x5000, data.size(), data);

    EXPECT_TRUE(MemoryScanner::scanBuffer(memory, Value(std::uint8_t{5}),
                                          ScanType::UNKNOWN)
                    .empty());
}

TEST(ScanBufferTest, RangeRequiresAnUpperBound) {
    const std::vector<std::byte> data{std::byte{1}, std::byte{2}};
    const MemoryRead memory(0x5000, data.size(), data);

    EXPECT_THROW(static_cast<void>(MemoryScanner::scanBuffer(
                     memory, Value(std::uint8_t{1}), ScanType::RANGE)),
                 std::invalid_argument);
}

TEST(ScanRegionTest, SupportsNumericRangeScan) {
    AnonymousMapping mapping(4096);
    const auto region = makeAnonymousRegion(mapping, 3);

    mapping.bytes()[0] = std::byte{1};
    mapping.bytes()[1] = std::byte{5};
    mapping.bytes()[2] = std::byte{9};

    MemoryScanner scanner(1);
    const auto results = scanner.scanRegion(
        getpid(), region, Value(std::uint8_t{1}), Value(std::uint8_t{5}));

    ASSERT_EQ(results.size(), 2);
    EXPECT_EQ(results[0].address(), mapping.address());
    EXPECT_EQ(results[1].address(), mapping.address() + 1);
}

TEST(ScanRegionTest, SupportsGreaterAndLessScans) {
    AnonymousMapping mapping(4096);
    const auto region = makeAnonymousRegion(mapping, 3);

    mapping.bytes()[0] = std::byte{1};
    mapping.bytes()[1] = std::byte{5};
    mapping.bytes()[2] = std::byte{9};

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

TEST(ScanRegionTest, findsValueSpanningChunkBoundary) {
    constexpr std::uint32_t value = 0x11223344;

    AnonymousMapping mapping(2 * DEFAULT_CHUNK_SIZE);
    const auto region = makeAnonymousRegion(mapping, 2 * DEFAULT_CHUNK_SIZE);

    const std::size_t offset = DEFAULT_CHUNK_SIZE - sizeof(value) / 2;

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

TEST(ScanProcessTest, findsPlantedValue) {
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

    const bool found = std::any_of(results.begin(), results.end(),
                                   [address](const ScanResult& result) {
                                       return result.address() == address;
                                   });

    EXPECT_TRUE(found);
}

TEST(ScanProcessTest, FindsPlantedValueWithRange) {
    AnonymousMapping mapping(4096);
    mapping.bytes()[0] = std::byte{9};
    const auto address = mapping.address();

    MemoryScanLevel level;
    level.memoryProtection = static_cast<std::uint8_t>(MemoryProtection::READ |
                                                       MemoryProtection::WRITE);
    level.memoryRegionType = static_cast<std::uint8_t>(
        MemoryRegionType::HEAP | MemoryRegionType::ANONYMOUS);

    MemoryScanner scanner;
    const auto results = scanner.scanProcess(
        getpid(), level, Value(std::uint8_t{8}), Value(std::uint8_t{10}));

    EXPECT_TRUE(std::any_of(results.begin(), results.end(),
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

    ASSERT_EQ(one.size(), 2);
    ASSERT_EQ(many.size(), one.size());
    ASSERT_EQ(one[0].address(), mapping.address() + firstOffset);
    ASSERT_EQ(one[1].address(), mapping.address() + secondOffset);
    EXPECT_EQ(many, one);
}

}  // namespace memseek
