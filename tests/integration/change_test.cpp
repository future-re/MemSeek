#include "memseek/memory/change.hpp"

#include <gtest/gtest.h>
#include <unistd.h>

#include <cstdint>
#include <utility>

#include "memseek/memory/scan.hpp"
#include "memseek/memory/snapshot.hpp"
#include "memseek/value.hpp"

namespace memseek {

TEST(CaptureChangesTest, RetainsCurrentValuesAndSupportsRepeatedFiltering) {
    std::uint32_t changing = 42;
    const std::uint32_t stable = 10;
    const auto changingAddress = reinterpret_cast<std::uintptr_t>(&changing);
    const auto stableAddress = reinterpret_cast<std::uintptr_t>(&stable);
    MemorySnapshot::Results results{
        ScanResult{changingAddress, Value(changing).data()},
        ScanResult{stableAddress, Value(stable).data()},
        ScanResult{0, Value(changing).data()},
    };
    const auto previous =
        MemorySnapshot::fromScanResults(getpid(), std::move(results));
    changing = 43;

    const auto changed = captureChanges(previous, ChangeType::CHANGED);
    ASSERT_TRUE(changed.has_value());
    ASSERT_EQ(changed->size(), 1);
    EXPECT_EQ(changed->results().at(0).address(), changingAddress);
    EXPECT_TRUE(Value(changing) == changed->results().at(0).data());
    EXPECT_TRUE(Value(std::uint32_t{42}) == previous.results().at(0).data());

    const auto unchanged = captureChanges(previous, ChangeType::UNCHANGED);
    ASSERT_TRUE(unchanged.has_value());
    ASSERT_EQ(unchanged->size(), 1);
    EXPECT_EQ(unchanged->results().at(0).address(), stableAddress);

    const auto next = captureChanges(*changed, ChangeType::CHANGED);
    ASSERT_TRUE(next.has_value());
    EXPECT_TRUE(next->empty());
    changing = 44;
    const auto again = captureChanges(*changed, ChangeType::CHANGED);
    ASSERT_TRUE(again.has_value());
    ASSERT_EQ(again->size(), 1);
    EXPECT_TRUE(Value(changing) == again->results().at(0).data());
}

TEST(CaptureChangesTest, ReportsInvalidProcessAndHandlesEmptyBaseline) {
    const auto invalid = MemorySnapshot::fromScanResults(-1, {});
    EXPECT_FALSE(captureChanges(invalid, ChangeType::CHANGED).has_value());
    const auto empty = MemorySnapshot::fromScanResults(getpid(), {});
    const auto result = captureChanges(empty, ChangeType::CHANGED);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->empty());
}

}  // namespace memseek
