#include "memseek/memory/reader.hpp"

#include <fcntl.h>
#include <sys/uio.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <limits>
#include <utility>

namespace {

auto readProcMem(const pid_t pid, const std::uintptr_t address,
                 std::byte* buffer, const std::size_t size)
    -> memseek::Result<std::size_t> {
    const auto path = std::format("/proc/{}/mem", pid);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    const int descriptor = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) {
        const auto errorCode = errno;  // error by open or format
        auto error =
            memseek::makeError(memseek::ErrorCode::PROCESS_ACCESS_FAILED,
                               std::format("open {} failed", path));
        error.systemError = {errorCode, std::generic_category()};
        error.pid = pid;
        error.address = address;
        error.size = size;
        return std::unexpected(std::move(error));
    }

    std::size_t total = 0;
    while (total < size) {
        const auto count = ::pread(descriptor, buffer + total, size - total,
                                   static_cast<off_t>(address + total));
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            const auto errorCode = errno;
            ::close(descriptor);
            auto error =
                memseek::makeError(memseek::ErrorCode::MEMORY_READ_FAILED,
                                   std::format("pread {} failed", path));
            error.systemError = {errorCode, std::generic_category()};
            error.pid = pid;
            error.address = address + total;
            error.size = size - total;
            return std::unexpected(std::move(error));
        }
        if (count == 0) {
            break;
        }
        total += static_cast<std::size_t>(count);
    }

    ::close(descriptor);
    return total;
}

}  // namespace

namespace memseek {

Result<MemoryRead> MemoryReader::read(const MemoryChunk& chunk,
                                      const MemoryRegion& region,
                                      std::size_t overlap) const {
#ifdef __linux__

    if (region.size >
        std::numeric_limits<std::uintptr_t>::max() - region.start) {
        auto error = makeError(ErrorCode::ADDRESS_OUT_OF_RANGE,
                               "memory region address range overflows");
        error.pid = m_pid;
        error.address = region.start;
        error.size = region.size;
        return std::unexpected(std::move(error));
    }

    const auto regionEnd = region.start + region.size;

    if (chunk.size == 0 || chunk.address < region.start ||
        chunk.address > regionEnd || chunk.size > regionEnd - chunk.address) {
        auto error = makeError(ErrorCode::ADDRESS_OUT_OF_RANGE,
                               "memory chunk is outside the region");
        error.pid = m_pid;
        error.address = chunk.address;
        error.size = chunk.size;
        return std::unexpected(std::move(error));
    }

    const auto maxReadable = regionEnd - chunk.address;
    const auto requestedSize =
        chunk.size > std::numeric_limits<std::size_t>::max() - overlap
            ? std::numeric_limits<std::size_t>::max()
            : chunk.size + overlap;
    const auto readSize = std::min(requestedSize, maxReadable);

    MemoryRead result{chunk.address, chunk.size,
                      std::vector<std::byte>(readSize), chunk.parentRegionId};

    iovec local{
        .iov_base = result.buffer().data(),
        .iov_len = result.size(),
    };

    iovec remote{
        .iov_base = reinterpret_cast<void*>(chunk.address),
        .iov_len = readSize,
    };

    auto bytesRead = ::process_vm_readv(m_pid, &local, 1, &remote, 1, 0);

    if (bytesRead < 0) {
        const auto processVmError = errno;
        const auto fallback =
            readProcMem(m_pid, chunk.address, result.buffer().data(), readSize);
        if (!fallback) {
            auto error = fallback.error();
            error.message = std::format(
                "process_vm_readv failed: {}; /proc memory fallback failed: {}",
                std::strerror(processVmError), error.message);
            error.code = ErrorCode::MEMORY_READ_FAILED;
            error.pid = m_pid;
            error.address = chunk.address;
            error.size = readSize;
            return std::unexpected(std::move(error));
        }
        bytesRead = static_cast<ssize_t>(*fallback);
    }

    if (std::cmp_not_equal(bytesRead, readSize)) {
        auto error =
            makeError(ErrorCode::SHORT_TRANSFER,
                      std::format("short read: expected {} bytes, received {}",
                                  readSize, bytesRead));
        error.pid = m_pid;
        error.address = chunk.address;
        error.size = readSize;
        error.bytesTransferred = static_cast<std::size_t>(bytesRead);
        return std::unexpected(std::move(error));
    }

    result.buffer().resize(static_cast<std::size_t>(bytesRead));

    return result;

#else

    return std::unexpected(makeError(ErrorCode::UNSUPPORTED_PLATFORM,
                                     "MemoryReader requires Linux"));

#endif
}
}  // namespace memseek
