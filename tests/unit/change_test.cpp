#include "memseek/memory/change.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

namespace memseek {

TEST(ChangeTest, ComparesCompleteByteValues) {
    const std::vector<std::byte> oldBytes{
        std::byte{1},
        std::byte{2},
    };
    const std::vector<std::byte> newBytes{
        std::byte{1},
        std::byte{3},
    };
    EXPECT_TRUE(matchesChange(oldBytes, newBytes, ChangeType::CHANGED));
    EXPECT_FALSE(matchesChange(oldBytes, newBytes, ChangeType::UNCHANGED));
    EXPECT_FALSE(matchesChange(oldBytes, oldBytes, ChangeType::CHANGED));
    EXPECT_TRUE(matchesChange(oldBytes, oldBytes, ChangeType::UNCHANGED));
}

TEST(ChangeTest, RejectsEmptyAndIncompleteValues) {
    const std::vector<std::byte> full{
        std::byte{1},
        std::byte{2},
    };
    const std::vector<std::byte> partial{std::byte{1}};
    for (const auto type : {ChangeType::CHANGED, ChangeType::UNCHANGED}) {
        EXPECT_FALSE(matchesChange({}, {}, type));
        EXPECT_FALSE(matchesChange(full, partial, type));
        EXPECT_FALSE(matchesChange(partial, full, type));
    }
}

}  // namespace memseek
