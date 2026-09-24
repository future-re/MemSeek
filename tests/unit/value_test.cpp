#include "memseek/value.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace memseek {

TEST(ValueTest, StoresIntegralValueBytesAndType) {
    constexpr std::uint32_t expected = 0x11223344;
    const Value value(expected);

    EXPECT_EQ(value.type(), ValueType::U_INT32);
    EXPECT_EQ(value.size(), sizeof(expected));
    EXPECT_EQ(value.as<std::uint32_t>(), expected);

    std::array<std::byte, sizeof(expected)> expectedBytes{};
    std::memcpy(expectedBytes.data(), &expected, sizeof(expected));
    EXPECT_TRUE(std::ranges::equal(value.data(), expectedBytes));
}

TEST(ValueTest, SupportsSignedAndFloatingValues) {
    const Value signedValue(std::int16_t{-1234});
    EXPECT_EQ(signedValue.type(), ValueType::INT16);
    EXPECT_EQ(signedValue.as<std::int16_t>(), -1234);

    const Value floatValue(3.25F);
    EXPECT_EQ(floatValue.type(), ValueType::FLOAT32);
    EXPECT_FLOAT_EQ(floatValue.as<float>(), 3.25F);

    const Value doubleValue(6.5);
    EXPECT_EQ(doubleValue.type(), ValueType::FLOAT64);
    EXPECT_DOUBLE_EQ(doubleValue.as<double>(), 6.5);
}

TEST(ValueTest, SupportsStringsAndByteArrays) {
    const Value stringValue(std::string{"hello"});
    EXPECT_EQ(stringValue.type(), ValueType::STR);
    EXPECT_EQ(stringValue.as<std::string>(), "hello");
    EXPECT_EQ(stringValue.asStringRef(), "hello");

    const std::vector<std::byte> bytes{std::byte{0x01}, std::byte{0x02},
                                       std::byte{0xff}};
    const Value byteValue(bytes);
    EXPECT_EQ(byteValue.type(), ValueType::BIN_ARRAY);
    EXPECT_EQ(byteValue.as<std::vector<std::byte>>(), bytes);
}

TEST(ValueTest, HandlesEmptyVariableLengthValues) {
    const Value emptyString(std::string{});
    EXPECT_TRUE(emptyString.empty());
    EXPECT_EQ(emptyString.asStringRef(), "");

    const Value emptyBytes(std::vector<std::byte>{});
    EXPECT_TRUE(emptyBytes.empty());
    EXPECT_TRUE(emptyBytes.as<std::vector<std::byte>>().empty());
}

TEST(ValueTest, RejectsReadingWithTheWrongType) {
    const Value value(std::uint32_t{42});

    EXPECT_THROW(value.as<std::uint64_t>(), std::runtime_error);
    EXPECT_THROW((void)value.asStringRef(), std::runtime_error);
}

}  // namespace memseek
