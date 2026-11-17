#include "memseek/app/scan_session.hpp"

#include <gtest/gtest.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

#include "memseek/memory/change.hpp"
#include "memseek/memory/region.hpp"
#include "memseek/memory/scan.hpp"
#include "memseek/memory/snapshot.hpp"
#include "memseek/value.hpp"

namespace memseek {

TEST(ScanSessionTest, ScansBigEndianAndFiltersRawSnapshotBytes) {
    std::array<std::byte, 4> bytes{
        std::byte{0x12},
        std::byte{0x34},
        std::byte{0x56},
        std::byte{0x78},
    };
    const auto address = reinterpret_cast<std::uintptr_t>(bytes.data());
    ScanSession session(getpid(), 1);
    MemoryScanLevel level;
    level.memoryRegionType = static_cast<std::uint8_t>(MemoryRegionType::STACK);
    ASSERT_TRUE(session
                    .scan(Value(std::uint32_t{0x12345678}), level,
                          ScanType::EXACT, ByteOrder::BIG)
                    .has_value());
    ASSERT_NE(session.snapshot(), nullptr);
    EXPECT_TRUE(std::ranges::any_of(session.snapshot()->results(),
                                    [address](const ScanResult& result) {
                                        return result.address() == address;
                                    }));
    bytes.at(3) = std::byte{0x79};
    ASSERT_TRUE(session.filter(ChangeType::CHANGED).has_value());
    const auto found = std::ranges::find_if(
        session.snapshot()->results(), [address](const ScanResult& result) {
            return result.address() == address;
        });
    ASSERT_NE(found, session.snapshot()->results().end());
    EXPECT_TRUE(std::ranges::equal(found->data(), bytes));
}

TEST(ScanSessionTest, ScansFiltersAndResetsBaseline) {
    std::uint64_t planted = 0x123456789abcdef0ULL;
    const auto address = reinterpret_cast<std::uintptr_t>(&planted);
    ScanSession session(getpid(), 1);
    ASSERT_EQ(session.snapshot(), nullptr);
    EXPECT_FALSE(session.filter(ChangeType::CHANGED).has_value());

    MemoryScanLevel level;
    level.memoryRegionType = static_cast<std::uint8_t>(MemoryRegionType::STACK);
    ASSERT_TRUE(session.scan(Value(planted), level).has_value());
    ASSERT_NE(session.snapshot(), nullptr);
    EXPECT_TRUE(std::ranges::any_of(session.snapshot()->results(),
                                    [address](const ScanResult& result) {
                                        return result.address() == address;
                                    }));

    ++planted;
    ASSERT_TRUE(session.filter(ChangeType::CHANGED).has_value());
    ASSERT_NE(session.snapshot(), nullptr);
    const auto found = std::ranges::find_if(
        session.snapshot()->results(), [address](const ScanResult& result) {
            return result.address() == address;
        });
    ASSERT_NE(found, session.snapshot()->results().end());
    EXPECT_TRUE(Value(planted) == found->data());

    // A rejected request must not replace the baseline.
    const auto previous = session.snapshot()->results();
    EXPECT_FALSE(session.scan(Value(std::uint64_t{1}), level, ScanType::UNKNOWN)
                     .has_value());
    EXPECT_EQ(session.snapshot()->results(), previous);

    session.setPid(getpid());
    EXPECT_NE(session.snapshot(), nullptr);
    session.setPid(-1);
    EXPECT_EQ(session.snapshot(), nullptr);
    EXPECT_FALSE(session.scan(Value(planted), level).has_value());
}

TEST(ScanSessionTest, EmptyBaselineRemainsValidUntilReset) {
    ScanSession session(getpid(), 1);
    MemoryScanLevel level;
    level.memoryRegionType = 0;
    ASSERT_TRUE(session.scan(Value(std::uint32_t{42}), level).has_value());
    ASSERT_NE(session.snapshot(), nullptr);
    EXPECT_TRUE(session.snapshot()->empty());
    ASSERT_TRUE(session.filter(ChangeType::CHANGED).has_value());
    ASSERT_NE(session.snapshot(), nullptr);
    EXPECT_TRUE(session.snapshot()->empty());
    session.reset();
    EXPECT_EQ(session.snapshot(), nullptr);
    EXPECT_FALSE(session.filter(ChangeType::UNCHANGED).has_value());
}

TEST(ScanSessionTest, RangeScanValidatesBoundsWithoutReplacingBaseline) {
    std::int32_t planted = 71234567;
    const auto address = reinterpret_cast<std::uintptr_t>(&planted);
    ScanSession session(getpid(), 1);
    MemoryScanLevel level;
    level.memoryProtection = static_cast<std::uint8_t>(MemoryProtection::READ);
    level.memoryRegionType = static_cast<std::uint8_t>(MemoryRegionType::STACK);

    ASSERT_TRUE(session
                    .scan(Value(std::int32_t{71234560}),
                          Value(std::int32_t{71234570}), level)
                    .has_value());
    ASSERT_NE(session.snapshot(), nullptr);
    EXPECT_TRUE(std::ranges::any_of(session.snapshot()->results(),
                                    [address](const ScanResult& result) {
                                        return result.address() == address;
                                    }));
    const auto baseline = session.snapshot()->results();

    EXPECT_FALSE(
        session.scan(Value(std::string{"a"}), Value(std::string{"z"}), level)
            .has_value());
    EXPECT_FALSE(
        session.scan(Value(std::uint16_t{1}), Value(std::uint32_t{2}), level)
            .has_value());
    EXPECT_FALSE(
        session.scan(Value(std::int32_t{9}), Value(std::int32_t{1}), level)
            .has_value());
    EXPECT_FALSE(session
                     .scan(Value(std::numeric_limits<double>::quiet_NaN()),
                           Value(1.0), level)
                     .has_value());
    EXPECT_EQ(session.snapshot()->results(), baseline);
}

TEST(ScanSessionTest, WritesOnlyCurrentUnchangedCandidates) {
    std::uint64_t planted = 0x1287abfedc456901ULL;
    const auto address = reinterpret_cast<std::uintptr_t>(&planted);
    ScanSession session(getpid(), 1);
    MemoryScanLevel level;
    level.memoryRegionType = static_cast<std::uint8_t>(MemoryRegionType::STACK);
    ASSERT_TRUE(session.scan(Value(planted), level));
    const auto& results = session.snapshot()->results();
    const auto candidate =
        std::ranges::find_if(results, [address](const ScanResult& result) {
            return result.address() == address;
        });
    ASSERT_NE(candidate, results.end());

    const auto copy = *candidate;
    const Value replacement(std::uint64_t{0x1122334455667788ULL});
    auto foreign = session.write(copy, replacement.data());
    ASSERT_FALSE(foreign);
    EXPECT_EQ(foreign.error().code, ErrorCode::INVALID_ARGUMENT);

    const std::array<std::byte, 9> tooLarge{};
    auto oversized = session.write(*candidate, tooLarge);
    ASSERT_FALSE(oversized);
    EXPECT_EQ(oversized.error().code, ErrorCode::INVALID_ARGUMENT);
    EXPECT_EQ(planted, 0x1287abfedc456901ULL);

    auto written = session.write(*candidate, replacement.data());
    ASSERT_TRUE(written) << (written ? "" : formatError(written.error()));
    EXPECT_EQ(*written, sizeof(planted));
    EXPECT_EQ(planted, 0x1122334455667788ULL);
    auto stale = session.write(*candidate, replacement.data());
    ASSERT_FALSE(stale);
    EXPECT_EQ(stale.error().code, ErrorCode::VALUE_CHANGED);
}

TEST(ScanSessionTest, RejectsWriteWithoutBaseline) {
    ScanSession session(getpid(), 1);
    const ScanResult result;
    const std::array<std::byte, 1> data{std::byte{1}};
    auto written = session.write(result, data);
    ASSERT_FALSE(written);
    EXPECT_EQ(written.error().code, ErrorCode::NO_BASELINE);
}

TEST(ScanSessionTest, RejectsWriteWhenMappingRangeChanges) {
    const auto pageSize = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
    const auto mappingSize = 2 * pageSize;
    void* mapping = ::mmap(nullptr, mappingSize, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ASSERT_NE(mapping, MAP_FAILED);
    struct Unmap {
        void* address;
        std::size_t size;
        ~Unmap() { ::munmap(address, size); }
    } unmap{mapping, mappingSize};

    const std::uint64_t planted = 0x987a1234bcdef601ULL;
    std::memcpy(mapping, &planted, sizeof(planted));
    ScanSession session(getpid(), 1);
    MemoryScanLevel level;
    level.memoryRegionType =
        static_cast<std::uint8_t>(MemoryRegionType::ANONYMOUS);
    ASSERT_TRUE(session.scan(Value(planted), level));
    const auto& results = session.snapshot()->results();
    const auto address = reinterpret_cast<std::uintptr_t>(mapping);
    const auto candidate =
        std::ranges::find_if(results, [address](const ScanResult& item) {
            return item.address() == address;
        });
    ASSERT_NE(candidate, results.end());
    // Split the original mapping while leaving the selected address writable.
    ASSERT_EQ(::mprotect(static_cast<std::byte*>(mapping) + pageSize, pageSize,
                         PROT_READ),
              0);

    const Value replacement(std::uint64_t{42});
    const auto written = session.write(*candidate, replacement.data());
    ASSERT_FALSE(written);
    EXPECT_EQ(written.error().code, ErrorCode::REGION_MISMATCH);
    std::uint64_t actual{};
    std::memcpy(&actual, mapping, sizeof(actual));
    EXPECT_EQ(actual, planted);
}

}  // namespace memseek
