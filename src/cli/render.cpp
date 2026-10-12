#include "memseek/cli/render.hpp"

#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>

#include "memseek/memory/region.hpp"

namespace memseek::cli {

namespace {

auto protectionText(const MemoryRegion& region) -> std::string {
    const auto value = static_cast<std::uint8_t>(region.protection);
    std::string result;
    result += (value & static_cast<std::uint8_t>(MemoryProtection::READ)) != 0
                  ? 'r'
                  : '-';
    result += (value & static_cast<std::uint8_t>(MemoryProtection::WRITE)) != 0
                  ? 'w'
                  : '-';
    result +=
        (value & static_cast<std::uint8_t>(MemoryProtection::EXECUTE)) != 0
            ? 'x'
            : '-';
    return result;
}

auto regionTypeText(const MemoryRegion& region) -> std::string_view {
    switch (region.regionType) {
        case MemoryRegionType::HEAP:
            return "heap";
        case MemoryRegionType::STACK:
            return "stack";
        case MemoryRegionType::ANONYMOUS:
            return "anonymous";
        case MemoryRegionType::MODULE:
            return "module";
        case MemoryRegionType::UNKNOWN:
            return "unknown";
    }
    return "unknown";
}

auto hexAddress(const std::uintptr_t address) -> std::string {
    std::ostringstream stream;
    stream << "0x" << std::hex << address;
    return stream.str();
}

auto bytesText(const std::span<const std::byte> bytes) -> std::string {
    std::ostringstream stream;
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        if (index != 0) {
            stream << ' ';
        }
        stream << std::hex << std::setw(2) << std::setfill('0')
               << std::to_integer<unsigned int>(bytes[index]);
    }
    return stream.str();
}

auto jsonEscape(const std::string_view text) -> std::string {
    std::ostringstream stream;
    for (const unsigned char character : text) {
        switch (character) {
            case '"':
                stream << "\\\"";
                break;
            case '\\':
                stream << "\\\\";
                break;
            case '\b':
                stream << "\\b";
                break;
            case '\f':
                stream << "\\f";
                break;
            case '\n':
                stream << "\\n";
                break;
            case '\r':
                stream << "\\r";
                break;
            case '\t':
                stream << "\\t";
                break;
            default:
                if (character < 0x20) {
                    stream << "\\u" << std::hex << std::setw(4)
                           << std::setfill('0')
                           << static_cast<unsigned int>(character);
                } else {
                    stream << static_cast<char>(character);
                }
        }
    }
    return stream.str();
}

}  // namespace

auto renderResultsText(const std::size_t candidateCount,
                       const std::span<const ResultEntry> entries)
    -> std::string {
    std::ostringstream output;
    output << "Candidates: " << candidateCount << " (showing " << entries.size()
           << ")\n"
           << "ADDRESS CURRENT PREVIOUS REGION TYPE PERMS PATH\n";
    for (const auto& entry : entries) {
        output << hexAddress(entry.address) << ' '
               << bytesText(entry.currentBytes) << ' ';
        if (entry.previousBytes) {
            output << bytesText(*entry.previousBytes);
        } else {
            output << '-';
        }
        if (entry.region) {
            output << ' ' << hexAddress(entry.region->start) << '-'
                   << hexAddress(entry.region->start + entry.region->size)
                   << ' ' << regionTypeText(*entry.region) << ' '
                   << protectionText(*entry.region) << ' '
                   << (entry.region->pathname.empty() ? "-"
                                                      : entry.region->pathname);
        } else {
            output << " unmapped unknown --- -";
        }
        output << '\n';
    }
    return output.str();
}

auto renderResultsJson(const pid_t pid, const std::size_t candidateCount,
                       const std::span<const ResultEntry> entries)
    -> std::string {
    std::ostringstream output;
    output << "{\n  \"pid\": " << pid
           << ",\n  \"candidate_count\": " << candidateCount
           << ",\n  \"returned_count\": " << entries.size()
           << ",\n  \"results\": [";
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const auto& entry = entries[index];
        output << (index == 0 ? "\n" : ",\n") << "    {\n"
               << "      \"address\": \"" << hexAddress(entry.address)
               << "\",\n"
               << "      \"current_bytes\": \"" << bytesText(entry.currentBytes)
               << "\",\n"
               << "      \"previous_bytes\": ";
        if (entry.previousBytes) {
            output << '"' << bytesText(*entry.previousBytes) << '"';
        } else {
            output << "null";
        }
        output << ",\n      \"region\": ";
        if (entry.region) {
            output << "{\n"
                   << "        \"start\": \"" << hexAddress(entry.region->start)
                   << "\",\n"
                   << "        \"end\": \""
                   << hexAddress(entry.region->start + entry.region->size)
                   << "\",\n"
                   << "        \"size\": " << entry.region->size << ",\n"
                   << "        \"protection\": \""
                   << protectionText(*entry.region) << "\",\n"
                   << "        \"type\": \"" << regionTypeText(*entry.region)
                   << "\",\n"
                   << "        \"path\": \""
                   << jsonEscape(entry.region->pathname) << "\"\n"
                   << "      }\n";
        } else {
            output << "null\n";
        }
        output << "    }";
    }
    if (!entries.empty()) {
        output << '\n';
    }
    output << "  ]\n}\n";
    return output.str();
}

auto renderRegionsText(const std::span<const MemoryRegion> regions)
    -> std::string {
    std::ostringstream output;
    output << "Regions: " << regions.size() << '\n'
           << "START END SIZE PERMS TYPE PATH\n";
    for (const auto& region : regions) {
        output << hexAddress(region.start) << ' '
               << hexAddress(region.start + region.size) << ' ' << region.size
               << ' ' << protectionText(region) << ' ' << regionTypeText(region)
               << ' ' << (region.pathname.empty() ? "-" : region.pathname)
               << '\n';
    }
    return output.str();
}

auto renderHelpText() -> std::string_view {
    return R"(Commands:
  attach <PID>
  scan <type> <value> [--region TYPE] [--protection PERM]
  scan <type> greater|less <value> [--region TYPE] [--protection PERM]
  scan <type> range <min> <max> [--region TYPE] [--protection PERM]
  scan --type TYPE [--mode exact] --value VALUE [filters]
  scan --type TYPE --mode greater|less --value VALUE [filters]
  scan --type TYPE --mode range --min MIN --max MAX [filters]
  filter changed|unchanged
  results [limit] [--format text|json]
  results [--limit N] [--format text|json]
  regions [--region heap|stack|anonymous|module]
  info
  reset
  help
  quit | exit

Types: u8 u16 u32 u64 i8 i16 i32 i64 float double string bytes
Filters: --region heap|stack|anonymous|module
         --protection read|write|execute
)";
}

}  // namespace memseek::cli
