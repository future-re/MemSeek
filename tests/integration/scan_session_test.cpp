#include "memseek/app/scan_session.hpp"

#include <gtest/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
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

}  // namespace memseek
