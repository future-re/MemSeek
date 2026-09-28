#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "common/scan_test_helper.hpp"
#include "memseek/memory/read.hpp"
#include "memseek/memory/scan.hpp"
#include "memseek/value.hpp"

namespace memseek {

TEST(ScanValidationTest, UnknownScanTypeReturnsNoMatches) {
    const auto data = test::encodeValue(std::uint8_t{5});
    const MemoryRead memory(0x5000, data.size(), data);

    EXPECT_TRUE(MemoryScanner::scanBuffer(memory, Value(std::uint8_t{5}),
                                          ScanType::UNKNOWN)
                    .empty());
}

TEST(ScanValidationTest, RangeRequiresAnUpperBound) {
    const std::vector<std::byte> data{std::byte{1}, std::byte{2}};
    const MemoryRead memory(0x5000, data.size(), data);

    EXPECT_THROW(static_cast<void>(MemoryScanner::scanBuffer(
                     memory, Value(std::uint8_t{1}), ScanType::RANGE)),
                 std::invalid_argument);
}

}  // namespace memseek
