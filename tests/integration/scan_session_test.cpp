#include "memseek/app/scan_session.hpp"

#include <gtest/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>

#include "memseek/memory/change.hpp"
#include "memseek/memory/region.hpp"
#include "memseek/memory/scan.hpp"
#include "memseek/memory/snapshot.hpp"
#include "memseek/value.hpp"

namespace memseek {

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

}  // namespace memseek
