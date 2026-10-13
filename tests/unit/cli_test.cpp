#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "memseek/cli/command.hpp"
#include "memseek/cli/render.hpp"

namespace memseek::cli {

namespace {

auto parse(const std::string& line) -> Command {
    auto parsed = parseCommandLine(line);
    if (!parsed) {
        throw std::runtime_error(parsed.error());
    }
    if (!*parsed) {
        throw std::runtime_error("expected a command");
    }
    return std::move(**parsed);
}

}  // namespace

TEST(CliParserTest, IgnoresWhitespaceAndRejectsUnterminatedQuotes) {
    const auto empty = parseCommandLine("  \t  ");
    ASSERT_TRUE(empty.has_value());
    EXPECT_FALSE(*empty);

    const auto quoted = parseCommandLine("scan string \"unterminated");
    ASSERT_FALSE(quoted.has_value());
    EXPECT_EQ(quoted.error(), "unterminated quote or escape");
}

TEST(CliParserTest, ParsesEveryExactValueType) {
    const std::vector<std::pair<std::string, ValueType>> cases{
        {"scan --type u8 --value 255", ValueType::U_INT8},
        {"scan --type u16 --value 65535", ValueType::U_INT16},
        {"scan --type u32 --value 4294967295", ValueType::U_INT32},
        {"scan --type u64 --value 18446744073709551615", ValueType::U_INT64},
        {"scan --type i8 --value -128", ValueType::INT8},
        {"scan --type i16 --value -32768", ValueType::INT16},
        {"scan --type i32 --value -2147483648", ValueType::INT32},
        {"scan --type i64 --value -9223372036854775808", ValueType::INT64},
        {"scan --type float --value 1.25", ValueType::FLOAT32},
        {"scan --type double --value -2.5", ValueType::FLOAT64},
        {"scan --type string --value \"hello world\"", ValueType::STR},
        {"scan --type bytes --value \"48 8B:05\"", ValueType::BIN_ARRAY},
    };

    for (const auto& [line, expectedType] : cases) {
        const auto command = parse(line);
        const auto* scan = std::get_if<ScanCommand>(&command);
        ASSERT_NE(scan, nullptr) << line;
        EXPECT_EQ(scan->scanType, ScanType::EXACT) << line;
        EXPECT_EQ(scan->lowerBound.type(), expectedType) << line;
    }
}

TEST(CliParserTest, ParsesNamedScanFilters) {
    const auto command = parse(
        "scan --type u32 --mode greater --value 100 "
        "--region heap --protection write");
    const auto* scan = std::get_if<ScanCommand>(&command);
    ASSERT_NE(scan, nullptr);
    EXPECT_EQ(scan->scanType, ScanType::GREATER);
    EXPECT_EQ(scan->lowerBound.as<std::uint32_t>(), 100U);
    EXPECT_EQ(scan->level.memoryRegionType,
              static_cast<std::uint8_t>(MemoryRegionType::HEAP));
    EXPECT_EQ(scan->level.memoryProtection,
              static_cast<std::uint8_t>(MemoryProtection::WRITE));
}

TEST(CliParserTest, ParsesNamedRangeOptionsInAnyOrder) {
    const auto command = parse(
        "scan --protection execute --max 9 --type i32 --mode range "
        "--min -2 --region stack");
    const auto* scan = std::get_if<ScanCommand>(&command);
    ASSERT_NE(scan, nullptr);
    ASSERT_TRUE(scan->upperBound.has_value());
    EXPECT_EQ(scan->scanType, ScanType::RANGE);
    EXPECT_EQ(scan->lowerBound.as<std::int32_t>(), -2);
    EXPECT_EQ(scan->upperBound->as<std::int32_t>(), 9);
    EXPECT_EQ(scan->level.memoryRegionType,
              static_cast<std::uint8_t>(MemoryRegionType::STACK));
    EXPECT_EQ(scan->level.memoryProtection,
              static_cast<std::uint8_t>(MemoryProtection::EXECUTE));
}

TEST(CliParserTest, UsesReadableKnownRegionsByDefault) {
    const auto command = parse("scan --type u16 --value 42");
    const auto* scan = std::get_if<ScanCommand>(&command);
    ASSERT_NE(scan, nullptr);
    EXPECT_EQ(scan->level.memoryProtection,
              static_cast<std::uint8_t>(MemoryProtection::READ));
    EXPECT_NE(scan->level.memoryRegionType &
                  static_cast<std::uint8_t>(MemoryRegionType::HEAP),
              0);
    EXPECT_NE(scan->level.memoryRegionType &
                  static_cast<std::uint8_t>(MemoryRegionType::MODULE),
              0);
}

TEST(CliParserTest, RejectsInvalidScanCombinationsAndValues) {
    const std::vector<std::string> invalid{
        "scan u32 1",
        "scan --type string --mode greater --value hello",
        "scan --type bytes --mode range --min 01 --max 02",
        "scan --type u32 --mode range --min 9 --max 1",
        "scan --type float --value nan",
        "scan --type double --value inf",
        "scan --type u8 --value 256",
        "scan --type bytes --value 123",
        "scan --type u32 --mode range --value 2 --min 1 --max 3",
        "scan --type u32 --mode exact --min 1 --max 3",
        "scan --type u32 --value 1 --region heap --region stack",
    };
    for (const auto& line : invalid) {
        EXPECT_FALSE(parseCommandLine(line).has_value()) << line;
    }
}

TEST(CliParserTest, ParsesResultsAndRejectsConflictingLimits) {
    const auto json = parse("results --format json");
    const auto* all = std::get_if<ResultsCommand>(&json);
    ASSERT_NE(all, nullptr);
    EXPECT_EQ(all->format, OutputFormat::JSON);
    EXPECT_FALSE(all->limit.has_value());

    const auto limited = parse("results 20 --format json");
    const auto* result = std::get_if<ResultsCommand>(&limited);
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->limit.has_value());
    EXPECT_EQ(*result->limit, 20);

    EXPECT_FALSE(parseCommandLine("results 20 --limit 10").has_value());
    EXPECT_FALSE(
        parseCommandLine("results --format json --format text").has_value());
}

TEST(CliRendererTest, RendersStableTextAndJsonFields) {
    const MemoryRegion region{
        .start = 0x1000,
        .size = 0x100,
        .protection = MemoryProtection::READ | MemoryProtection::WRITE,
        .regionType = MemoryRegionType::HEAP,
        .pathname = "a\"b\n",
    };
    const std::vector<ResultEntry> entries{
        ResultEntry{
            .address = 0x1010,
            .currentBytes = {std::byte{0x12}, std::byte{0xab}},
            .previousBytes =
                std::vector<std::byte>{std::byte{0x10}, std::byte{0xab}},
            .region = region,
        },
        ResultEntry{
            .address = 0x9000,
            .currentBytes = {std::byte{0xff}},
            .previousBytes = std::nullopt,
            .region = std::nullopt,
        },
    };

    const auto text = renderResultsText(5, entries);
    EXPECT_NE(text.find("Candidates: 5 (showing 2)"), std::string::npos);
    EXPECT_NE(text.find("0x1010 12 ab 10 ab 0x1000-0x1100 heap rw-"),
              std::string::npos);
    EXPECT_NE(text.find("0x9000 ff - unmapped"), std::string::npos);

    const auto json = renderResultsJson(123, 5, entries);
    EXPECT_NE(json.find("\"pid\": 123"), std::string::npos);
    EXPECT_NE(json.find("\"candidate_count\": 5"), std::string::npos);
    EXPECT_NE(json.find("\"returned_count\": 2"), std::string::npos);
    EXPECT_NE(json.find("\"address\": \"0x1010\""), std::string::npos);
    EXPECT_NE(json.find("\"current_bytes\": \"12 ab\""), std::string::npos);
    EXPECT_NE(json.find("\"previous_bytes\": null"), std::string::npos);
    EXPECT_NE(json.find("\"path\": \"a\\\"b\\n\""), std::string::npos);
    EXPECT_NE(json.find("\"region\": null"), std::string::npos);
}

}  // namespace memseek::cli
