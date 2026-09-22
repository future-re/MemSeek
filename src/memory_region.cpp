#include "memseek/memory_region.hpp"

#include <fstream>
#include <sstream>

namespace memseek {

namespace {

[[nodiscard]]
auto regionMatchesLevel(const MemoryRegion& region,
                        const MemoryScanLevel& level) noexcept -> bool {
    const auto protection = static_cast<std::uint8_t>(region.protection);
    const auto regionType = static_cast<std::uint8_t>(region.regionType);

    // A region can only be scanned if it is readable, regardless of the
    // requested protection mask.
    if ((protection & static_cast<std::uint8_t>(MemoryProtection::READ)) == 0) {
        return false;
    }

    // Every requested protection bit must be present.
    if ((protection & level.memoryProtection) != level.memoryProtection) {
        return false;
    }

    // The region type must intersect the requested type mask.
    return (regionType & level.memoryRegionType) != 0;
}

}  // namespace

std::expected<MemoryRegionList, std::string> readProcess(
    pid_t pid, MemoryScanLevel level) {
    MemoryRegionList regionList{};

#if defined(__linux__)

    const std::string mapsPath = "/proc/" + std::to_string(pid) + "/maps";

    std::ifstream mapsFile(mapsPath);
    if (!mapsFile.is_open()) {
        return std::unexpected("failed to open: " + mapsPath);
    }

    std::string line;
    uint64_t regionId = 0;

    while (std::getline(mapsFile, line)) {
        std::istringstream stream(line);

        std::string addressRange;
        std::string permissions;
        std::string offset;
        std::string device;
        std::uint64_t inode{};
        std::string pathname;

        if (!(stream >> addressRange >> permissions >> offset >> device >>
              inode)) {
            continue;
        }

        // pathname is optional, and may contain spaces, so read the rest of the
        // line as a whole.
        std::getline(stream, pathname);
        regionId++;

        if (!pathname.empty()) {
            const auto first = pathname.find_first_not_of(' ');

            if (first != std::string::npos) {
                pathname.erase(0, first);
            } else {
                pathname.clear();
            }
        }

        // -------------------------
        // decode start-end
        // -------------------------

        const auto dash = addressRange.find('-');
        if (dash == std::string::npos) {
            continue;
        }

        const auto start =
            std::stoull(addressRange.substr(0, dash), nullptr, 16);

        const auto end =
            std::stoull(addressRange.substr(dash + 1), nullptr, 16);

        if (end <= start) {
            continue;
        }

        MemoryRegion region{};

        region.start = start;

        region.size = static_cast<std::size_t>(end - start);

        region.pathname = pathname;

        region.id = regionId;

        // -------------------------
        // decode protection: rwx
        // -------------------------

        region.protection = MemoryProtection::NONE;

        if (permissions.size() >= 3) {
            if (permissions[0] == 'r') {
                region.protection |= MemoryProtection::READ;
            }

            if (permissions[1] == 'w') {
                region.protection |= MemoryProtection::WRITE;
            }

            if (permissions[2] == 'x') {
                region.protection |= MemoryProtection::EXECUTE;
            }
        }

        // -------------------------
        // decode region type
        // -------------------------

        if (pathname == "[heap]") {
            region.regionType = MemoryRegionType::HEAP;
        } else if (pathname.starts_with("[stack")) {
            region.regionType = MemoryRegionType::STACK;
        } else if (pathname.empty()) {
            region.regionType = MemoryRegionType::ANONYMOUS;
        } else if (!pathname.empty() && pathname.front() == '/') {
            region.regionType = MemoryRegionType::MODULE;
        } else {
            region.regionType = MemoryRegionType::UNKNOWN;
        }

        if (!regionMatchesLevel(region, level)) {
            continue;
        }

        regionList.addRegion(std::move(region));
    }

#else

    return std::unexpected("unsupported platform");

#endif

    return regionList;
}
}  // namespace memseek