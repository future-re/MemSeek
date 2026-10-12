#pragma once

#include <sys/types.h>

#include <cstddef>
#include <istream>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "memseek/app/scan_session.hpp"
#include "memseek/cli/command.hpp"

namespace memseek::cli {

struct Settings {
    std::size_t workers{1};
    ByteOrder byteOrder{ByteOrder::NATIVE};
};

class InteractiveCli {
   public:
    InteractiveCli(Settings settings, std::istream& input, std::ostream& output,
                   std::ostream& errorOutput);

    auto run() -> int;

    // Returns false when the command requests that the interactive loop stop.
    auto executeLine(std::string_view line) -> bool;

   private:
    auto execute(const Command& command) -> bool;
    auto executeAttach(const AttachCommand& command) -> void;
    auto executeScan(const ScanCommand& command) -> void;
    auto executeFilter(const FilterCommand& command) -> void;
    auto executeResults(const ResultsCommand& command) -> void;
    auto executeRegions(const RegionsCommand& command) -> void;
    auto executeInfo() -> void;
    auto executeReset() -> void;
    [[nodiscard]] auto requireSession() const -> ScanSession&;
    auto clearScanState() -> void;

    Settings m_settings;
    std::istream& m_input;
    std::ostream& m_output;
    std::ostream& m_errorOutput;
    std::unique_ptr<ScanSession> m_session;
    pid_t m_pid{};
    std::string m_lastScan;
    std::vector<std::optional<std::vector<std::byte>>> m_previousBytes;
};

}  // namespace memseek::cli
