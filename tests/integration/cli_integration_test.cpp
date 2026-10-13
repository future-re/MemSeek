#include <gtest/gtest.h>
#include <unistd.h>

#include <cstdint>
#include <sstream>
#include <string>

#include "memseek/cli/interactive.hpp"

namespace memseek::cli {

TEST(CliIntegrationTest, PreservesAttachmentWhenNextAttachFails) {
    std::istringstream input;
    std::ostringstream output;
    std::ostringstream errors;
    InteractiveCli cli{Settings{.workers = 1}, input, output, errors};

    EXPECT_TRUE(cli.executeLine("   "));
    EXPECT_TRUE(cli.executeLine("results"));
    EXPECT_NE(errors.str().find("attach to a process first"),
              std::string::npos);

    ASSERT_TRUE(cli.executeLine("attach " + std::to_string(getpid())));
    ASSERT_TRUE(cli.executeLine("attach 2147483647"));
    ASSERT_TRUE(cli.executeLine("info"));

    EXPECT_NE(
        output.str().find("Attached to process " + std::to_string(getpid())),
        std::string::npos);
    EXPECT_NE(output.str().find("PID: " + std::to_string(getpid())),
              std::string::npos);
    EXPECT_NE(errors.str().find("failed to open"), std::string::npos);
}

TEST(CliIntegrationTest, ScansFiltersRendersAndResetsSelfProcess) {
    std::uint64_t planted = 0x6f21a49bc38d570eULL;
    std::istringstream input;
    std::ostringstream output;
    std::ostringstream errors;
    InteractiveCli cli{Settings{.workers = 1, .byteOrder = ByteOrder::NATIVE},
                       input, output, errors};

    ASSERT_TRUE(cli.executeLine("attach " + std::to_string(getpid())));
    ASSERT_TRUE(cli.executeLine("scan --type u64 --value " +
                                std::to_string(planted) + " --region stack"));
    ++planted;
    ASSERT_TRUE(cli.executeLine("filter changed"));
    ASSERT_TRUE(cli.executeLine("filter unchanged"));
    ASSERT_TRUE(cli.executeLine("results --limit 5 --format json"));
    ASSERT_TRUE(cli.executeLine("info"));
    ASSERT_TRUE(cli.executeLine("reset"));
    ASSERT_TRUE(cli.executeLine("results"));
    EXPECT_FALSE(cli.executeLine("quit"));

    EXPECT_EQ(errors.str(), "Error: no baseline available; run scan first\n");
    EXPECT_NE(output.str().find("Found "), std::string::npos);
    EXPECT_NE(output.str().find(" candidates\n"), std::string::npos);
    EXPECT_NE(output.str().find("\"candidate_count\":"), std::string::npos);
    EXPECT_NE(output.str().find("\"previous_bytes\": \""), std::string::npos);
    EXPECT_NE(output.str().find("State: scanned"), std::string::npos);
    EXPECT_NE(output.str().find("Scan session reset."), std::string::npos);
    EXPECT_NE(errors.str().find("no baseline available; run scan first"),
              std::string::npos);
}

}  // namespace memseek::cli
