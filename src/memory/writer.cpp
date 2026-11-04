#include "memseek/memory/writer.hpp"

#include <sys/uio.h>

#include <algorithm>
#include <cerrno>
#include <format>
#include <limits>
#include <string>
#include <utility>

#include "memseek/memory/region.hpp"

namespace memseek {

MemoryWriter::MemoryWriter(pid_t pid) noexcept : m_pid{pid} {}

auto MemoryWriter::write(const MemoryWriteTarget& target,
                         std::span<const std::byte> data)
    -> Result<std::size_t> {
#ifdef __linux__
    const auto makeWriteError = [&](const ErrorCode code, std::string message,
                                    const std::size_t bytesWritten = 0) {
        auto error = makeError(code, std::move(message));
        error.pid = m_pid;
        error.address = target.address;
        error.size = data.size();
        error.bytesTransferred = bytesWritten;
        return error;
    };

    if (data.size() > target.size) {
        return std::unexpected(makeWriteError(
            ErrorCode::INVALID_ARGUMENT, "write data exceeds target size"));
    }
    if (data.empty()) {
        return std::size_t{0};
    }
    if (target.address >
        std::numeric_limits<std::uintptr_t>::max() - data.size()) {
        return std::unexpected(makeWriteError(ErrorCode::ADDRESS_OUT_OF_RANGE,
                                              "write address range overflows"));
    }

    MemoryScanLevel writableLevel;
    writableLevel.memoryProtection =
        static_cast<std::uint8_t>(MemoryProtection::WRITE);
    writableLevel.memoryRegionType =
        static_cast<std::uint8_t>(MemoryRegionType::HEAP) |
        static_cast<std::uint8_t>(MemoryRegionType::STACK) |
        static_cast<std::uint8_t>(MemoryRegionType::ANONYMOUS) |
        static_cast<std::uint8_t>(MemoryRegionType::MODULE);

    // This is a snapshot; the target can still change mappings before the
    // write.
    const auto regions = readProcess(m_pid, writableLevel, false);
    if (!regions) {
        auto error = regions.error();
        error.message =
            std::format("failed to inspect target mappings: {}", error.message);
        error.address = target.address;
        error.size = data.size();
        return std::unexpected(std::move(error));
    }

    const auto region = std::ranges::find_if(
        regions->getRegions(), [&target, &data](const MemoryRegion& candidate) {
            if (target.address < candidate.start) {
                return false;
            }
            const auto offset = target.address - candidate.start;
            return offset <= candidate.size &&
                   data.size() <= candidate.size - offset;
        });
    if (region == regions->getRegions().end()) {
        return std::unexpected(makeWriteError(
            ErrorCode::MAPPING_NOT_WRITABLE,
            "write range is not contained in one writable mapping"));
    }
    if (target.parentRegionId != 0 && target.parentRegionId != region->id) {
        return std::unexpected(makeWriteError(
            ErrorCode::REGION_MISMATCH,
            "target region id does not match the writable mapping"));
    }

    std::size_t totalWritten = 0;
    while (totalWritten < data.size()) {
        iovec local{
            .iov_base =
                const_cast<std::byte*>(data.data() + totalWritten),  // NOLINT
            .iov_len = data.size() - totalWritten,
        };
        iovec remote{
            .iov_base = reinterpret_cast<void*>(target.address + totalWritten),
            .iov_len = data.size() - totalWritten,
        };

        const auto written =
            ::process_vm_writev(m_pid, &local, 1, &remote, 1, 0);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            const auto errorCode = errno;
            auto error = makeWriteError(
                ErrorCode::MEMORY_WRITE_FAILED,
                "process_vm_writev failed after a partial write", totalWritten);
            error.systemError = {errorCode, std::generic_category()};
            return std::unexpected(std::move(error));
        }
        if (written == 0) {
            return std::unexpected(makeWriteError(
                ErrorCode::SHORT_TRANSFER,
                "process_vm_writev stopped before completing the write",
                totalWritten));
        }

        totalWritten += static_cast<std::size_t>(written);
    }

    return totalWritten;
#else
    (void)target;
    (void)data;
    return std::unexpected(makeError(ErrorCode::UNSUPPORTED_PLATFORM,
                                     "MemoryWriter requires Linux"));
#endif
}

}  // namespace memseek
