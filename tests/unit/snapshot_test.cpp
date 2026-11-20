#include "memseek/memory/snapshot.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <utility>
#include <vector>

#include "memseek/memory/scan.hpp"

namespace memseek {
namespace {

TEST(MemorySnapshotTest, StoresTheCompleteScanResultSet) {
    const std::vector<ScanResult> results{
        ScanResult{0x1000,
                   std::vector<std::byte>{std::byte{0x01}, std::byte{0x02}}},
        ScanResult{0x2000, std::vector<std::byte>{std::byte{0x2a}}},
    };
    const auto snapshot = MemorySnapshot(1234, results);

    ASSERT_EQ(snapshot.pid(), 1234);
    ASSERT_EQ(snapshot.size(), results.size());
    ASSERT_EQ(snapshot.results().size(), 2);
    EXPECT_EQ(snapshot.results().at(0).address(), results.at(0).address());
    const std::vector<std::byte> firstExpected{
        std::byte{0x01},
        std::byte{0x02},
    };
    EXPECT_TRUE(
        std::ranges::equal(snapshot.results().at(0).data(), firstExpected));
    EXPECT_EQ(snapshot.results().at(1).address(), results.at(1).address());
}

TEST(MemorySnapshotTest, CopiesScanResultValues) {
    std::vector<std::byte> bytes{std::byte{0x10}};
    const std::vector<ScanResult> results{ScanResult{0x1000, bytes}};
    const auto snapshot = MemorySnapshot(1234, results);
    bytes.at(0) = std::byte{0x20};

    EXPECT_EQ(snapshot.results().front().data().front(), std::byte{0x10});
}

TEST(MemorySnapshotTest, MovesLargeResultSetIntoSnapshot) {
    const std::vector<ScanResult> results{
        ScanResult{0x1000, std::vector<std::byte>{std::byte{0x10}}},
        ScanResult{0x1000,
                   std::vector<std::byte>{std::byte{0x30}, std::byte{0x40}}},
    };
    auto movableResults = results;
    const auto snapshot = MemorySnapshot(1234, std::move(movableResults));

    ASSERT_EQ(snapshot.size(), 2);
    EXPECT_EQ(snapshot.results().at(1).address(), 0x1000);
}

TEST(MemorySnapshotTest, CopiesShareImmutableResults) {
    const std::array bytes{std::byte{0x42}};
    const MemorySnapshot snapshot(1234, {ScanResult(0x1000, bytes)});
    const auto copy = snapshot;
    EXPECT_EQ(&copy.results(), &snapshot.results());
    EXPECT_EQ(copy.results().data(), snapshot.results().data());

    auto retained = copy;
    {
        MemorySnapshot temporary(5678, {ScanResult(0x2000, bytes)});
        retained = temporary;
    }
    ASSERT_EQ(retained.size(), 1);
    EXPECT_EQ(retained.pid(), 5678);
    EXPECT_EQ(retained.results().front().data().front(), std::byte{0x42});
}

TEST(ScanResultTest, EqualityComparesValuesAndMovesLeaveEmptyData) {
    const std::array bytes{std::byte{1}, std::byte{2}};
    ScanResult first(0x1000, bytes, 7);
    const ScanResult equal(0x1000, bytes, 7);
    EXPECT_NE(first.data().data(), equal.data().data());
    EXPECT_EQ(first, equal);
    EXPECT_NE(first, ScanResult(0x1001, bytes, 7));
    EXPECT_NE(first, ScanResult(0x1000, bytes, 8));
    EXPECT_NE(first, ScanResult(0x1000, {}, 7));

    const auto copy = first;
    EXPECT_EQ(copy.data().data(), first.data().data());
    auto moved = std::move(first);
    EXPECT_EQ(moved, equal);
    EXPECT_TRUE(first.data().empty());
    first = std::move(moved);
    EXPECT_EQ(first, equal);
    EXPECT_TRUE(moved.data().empty());
    EXPECT_TRUE(ScanResult{}.data().empty());
}

}  // namespace
}  // namespace memseek
