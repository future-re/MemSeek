#include "memseek/cli/interactive.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <istream>
#include <memory>
#include <optional>
#include <ostream>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "memseek/cli/command.hpp"
#include "memseek/cli/render.hpp"
#include "memseek/memory/change.hpp"
#include "memseek/memory/region.hpp"
#include "memseek/memory/scan.hpp"
#include "memseek/memory/snapshot.hpp"
#include "memseek/utils/version.hpp"

namespace memseek::cli {

namespace {

constexpr auto allRegionTypes() noexcept -> std::uint8_t {
    return static_cast<std::uint8_t>(MemoryRegionType::HEAP) |
           static_cast<std::uint8_t>(MemoryRegionType::STACK) |
           static_cast<std::uint8_t>(MemoryRegionType::ANONYMOUS) |
           static_cast<std::uint8_t>(MemoryRegionType::MODULE);
}

auto readableLevel() -> MemoryScanLevel {
    return MemoryScanLevel{
        .memoryProtection = static_cast<std::uint8_t>(MemoryProtection::READ),
        .memoryRegionType = allRegionTypes(),
    };
}

auto findRegion(const std::vector<MemoryRegion>& regions,
                const std::uintptr_t address,
                const std::size_t width) -> std::optional<MemoryRegion> {
    auto iterator =
        std::ranges::upper_bound(regions, address, {}, &MemoryRegion::start);
    if (iterator == regions.begin()) {
        return std::nullopt;
    }
    --iterator;
    if (address < iterator->start ||
        address - iterator->start >= iterator->size ||
        width > iterator->size - (address - iterator->start)) {
        return std::nullopt;
    }
    return *iterator;
}

}  // namespace

InteractiveCli::InteractiveCli(Settings settings, std::istream& input,
                               std::ostream& output, std::ostream& errorOutput)
    : m_settings(settings),
      m_input(input),
      m_output(output),
      m_errorOutput(errorOutput) {
    if (m_settings.workers == 0) {
        m_settings.workers = 1;
    }
}

auto InteractiveCli::run() -> int {
    m_output << "MemSeek interactive CLI " << version::string() << '\n'
             << "Type 'help' for commands.\n";
    std::string line;
    while (true) {
        m_output << (m_pid == 0 ? "memseek> "
                                : "memseek[" + std::to_string(m_pid) + "]> ")
                 << std::flush;
        if (!std::getline(m_input, line) || !executeLine(line)) {
            break;
        }
    }
    return 0;
}

auto InteractiveCli::executeLine(const std::string_view line) -> bool {
    try {
        auto parsed = parseCommandLine(line);
        if (!parsed) {
            throw std::runtime_error(parsed.error());
        }
        if (!*parsed) {
            return true;
        }
        return execute(**parsed);
    } catch (const std::exception& error) {
        m_errorOutput << "Error: " << error.what() << '\n';
        return true;
    }
}

auto InteractiveCli::execute(const Command& command) -> bool {
    if (const auto* value = std::get_if<AttachCommand>(&command)) {
        executeAttach(*value);
    } else if (const auto* value = std::get_if<ScanCommand>(&command)) {
        executeScan(*value);
    } else if (const auto* value = std::get_if<FilterCommand>(&command)) {
        executeFilter(*value);
    } else if (const auto* value = std::get_if<ResultsCommand>(&command)) {
        executeResults(*value);
    } else if (const auto* value = std::get_if<RegionsCommand>(&command)) {
        executeRegions(*value);
    } else if (std::holds_alternative<InfoCommand>(command)) {
        executeInfo();
    } else if (std::holds_alternative<ResetCommand>(command)) {
        executeReset();
    } else if (std::holds_alternative<HelpCommand>(command)) {
        m_output << renderHelpText();
    } else if (std::holds_alternative<QuitCommand>(command)) {
        return false;
    }
    return true;
}

auto InteractiveCli::executeAttach(const AttachCommand& command) -> void {
    const auto regions = readProcess(command.pid, readableLevel());
    if (!regions) {
        throw std::runtime_error(regions.error());
    }

    auto nextSession =
        std::make_unique<ScanSession>(command.pid, m_settings.workers);
    m_session = std::move(nextSession);
    m_pid = command.pid;
    clearScanState();
    m_output << "Attached to process " << m_pid << '\n';
}

auto InteractiveCli::executeScan(const ScanCommand& command) -> void {
    auto& session = requireSession();
    std::expected<void, std::string> result;
    if (command.scanType == ScanType::RANGE) {
        if (!command.upperBound) {
            throw std::runtime_error("range scan requires an upper bound");
        }
        result = session.scan(command.lowerBound, *command.upperBound,
                              command.level, m_settings.byteOrder);
    } else {
        result = session.scan(command.lowerBound, command.level,
                              command.scanType, m_settings.byteOrder);
    }
    if (!result) {
        throw std::runtime_error(result.error());
    }

    m_previousBytes.clear();
    m_lastScan = command.description + " byte-order=" +
                 std::string{byteOrderText(m_settings.byteOrder)};
    m_output << "Found " << session.snapshot()->size() << " candidates\n";
}

auto InteractiveCli::executeFilter(const FilterCommand& command) -> void {
    auto& session = requireSession();
    const auto* baseline = session.snapshot();
    if (baseline == nullptr) {
        throw std::runtime_error("no baseline available; run scan first");
    }
    const auto previousResults = baseline->results();
    const auto before = previousResults.size();
    const auto result = session.filter(command.type);
    if (!result) {
        throw std::runtime_error(result.error());
    }

    const auto& currentResults = session.snapshot()->results();
    m_previousBytes.clear();
    m_previousBytes.reserve(currentResults.size());
    std::size_t previousIndex = 0;
    for (const auto& current : currentResults) {
        while (previousIndex < previousResults.size() &&
               previousResults[previousIndex].address() < current.address()) {
            ++previousIndex;
        }
        if (previousIndex < previousResults.size() &&
            previousResults[previousIndex].address() == current.address()) {
            const auto bytes = previousResults[previousIndex].data();
            m_previousBytes.emplace_back(
                std::vector<std::byte>{bytes.begin(), bytes.end()});
        } else {
            m_previousBytes.emplace_back(std::nullopt);
        }
    }
    m_output << before << " -> " << currentResults.size() << " candidates\n";
}

auto InteractiveCli::executeResults(const ResultsCommand& command) -> void {
    auto& session = requireSession();
    const auto* snapshot = session.snapshot();
    if (snapshot == nullptr) {
        throw std::runtime_error("no baseline available; run scan first");
    }

    const auto regions = readProcess(m_pid, readableLevel());
    if (!regions) {
        throw std::runtime_error(regions.error());
    }
    const auto defaultLimit = command.format == OutputFormat::TEXT
                                  ? std::size_t{20}
                                  : snapshot->size();
    const auto count =
        std::min(command.limit.value_or(defaultLimit), snapshot->size());
    std::vector<ResultEntry> entries;
    entries.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const auto& result = snapshot->results()[index];
        const auto bytes = result.data();
        std::optional<std::vector<std::byte>> previous;
        if (index < m_previousBytes.size()) {
            previous = m_previousBytes[index];
        }
        entries.push_back(ResultEntry{
            .address = result.address(),
            .currentBytes = {bytes.begin(), bytes.end()},
            .previousBytes = std::move(previous),
            .region = findRegion(regions->getRegions(), result.address(),
                                 bytes.size()),
        });
    }

    if (command.format == OutputFormat::JSON) {
        m_output << renderResultsJson(m_pid, snapshot->size(), entries);
    } else {
        m_output << renderResultsText(snapshot->size(), entries);
    }
}

auto InteractiveCli::executeRegions(const RegionsCommand& command) -> void {
    static_cast<void>(requireSession());
    const auto regions = readProcess(m_pid, command.level);
    if (!regions) {
        throw std::runtime_error(regions.error());
    }
    m_output << renderRegionsText(regions->getRegions());
}

auto InteractiveCli::executeInfo() -> void {
    auto& session = requireSession();
    const auto* snapshot = session.snapshot();
    m_output << "PID: " << m_pid << '\n'
             << "State: " << (snapshot == nullptr ? "attached" : "scanned")
             << '\n'
             << "Candidates: " << (snapshot == nullptr ? 0 : snapshot->size())
             << '\n'
             << "Last scan: " << (m_lastScan.empty() ? "none" : m_lastScan)
             << '\n'
             << "Byte order: " << byteOrderText(m_settings.byteOrder) << '\n';
}

auto InteractiveCli::executeReset() -> void {
    auto& session = requireSession();
    session.reset();
    clearScanState();
    m_output << "Scan session reset.\n";
}

auto InteractiveCli::requireSession() const -> ScanSession& {
    if (!m_session) {
        throw std::runtime_error("attach to a process first");
    }
    return *m_session;
}

auto InteractiveCli::clearScanState() -> void {
    m_lastScan.clear();
    m_previousBytes.clear();
}

}  // namespace memseek::cli
