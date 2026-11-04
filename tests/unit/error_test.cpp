#include "memseek/error.hpp"

#include <gtest/gtest.h>

namespace memseek {

TEST(MemSeekErrorTest, FormatsCodeAndContext) {
    auto error = makeError(ErrorCode::SHORT_TRANSFER, "read was incomplete");
    error.pid = 42;
    error.address = 0x1234;
    error.size = 16;
    error.bytesTransferred = 8;

    const auto formatted = formatError(error);

    EXPECT_NE(formatted.find("[short_transfer]"), std::string::npos);
    EXPECT_NE(formatted.find("pid=42"), std::string::npos);
    EXPECT_NE(formatted.find("address=0x1234"), std::string::npos);
    EXPECT_NE(formatted.find("size=16"), std::string::npos);
    EXPECT_NE(formatted.find("bytes_transferred=8"), std::string::npos);
}

TEST(MemSeekErrorTest, KeepsSystemErrorStructured) {
    auto error = makeError(ErrorCode::PROCESS_ACCESS_FAILED, "cannot open map");
    error.systemError = {13, std::generic_category()};

    EXPECT_EQ(error.systemError.value(), 13);
    EXPECT_NE(formatError(error).find("system_error="), std::string::npos);
}

}  // namespace memseek
