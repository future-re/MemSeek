#include "memseek/memory/snapshot.hpp"

#include <gtest/gtest.h>

#include <algorithm>
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
    const auto snapshot = MemorySnapshot::fromScanResults(1234, results);

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
    const auto snapshot = MemorySnapshot::fromScanResults(1234, results);
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
    const auto snapshot =
        MemorySnapshot::fromScanResults(1234, std::move(movableResults));

    ASSERT_EQ(snapshot.size(), 2);
    EXPECT_EQ(snapshot.results().at(1).address(), 0x1000);
}

}  // namespace
}  // namespace memseek
