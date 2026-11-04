#include "memseek/memory/reader.hpp"

#include <fcntl.h>
#include <sys/uio.h>
#include <unistd.h>

#include <cerrno>
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

auto MemoryReader::read(const std::uintptr_t address,
                        const std::size_t size) const
    -> Result<std::vector<std::byte>> {
#ifdef __linux__

    if (size > std::numeric_limits<std::uintptr_t>::max() - address) {
        auto error = makeError(ErrorCode::ADDRESS_OUT_OF_RANGE,
                               "memory read address range overflows");
        error.pid = m_pid;
        error.address = address;
        error.size = size;
        return std::unexpected(std::move(error));
    }

    if (size == 0) {
        return std::vector<std::byte>{};
    }

    std::vector<std::byte> result(size);

    iovec local{
        .iov_base = result.data(),
        .iov_len = result.size(),
    };

    iovec remote{
        .iov_base = reinterpret_cast<void*>(address),
        .iov_len = size,
    };

    auto bytesRead = ::process_vm_readv(m_pid, &local, 1, &remote, 1, 0);

    if (bytesRead < 0) {
        const auto processVmError = errno;
        const auto fallback = readProcMem(m_pid, address, result.data(), size);
        if (!fallback) {
            auto error = fallback.error();
            error.message = std::format(
                "process_vm_readv failed: {}; /proc memory fallback failed: {}",
                std::strerror(processVmError), error.message);
            error.code = ErrorCode::MEMORY_READ_FAILED;
            error.pid = m_pid;
            error.address = address;
            error.size = size;
            return std::unexpected(std::move(error));
        }
        bytesRead = static_cast<ssize_t>(*fallback);
    }

    if (std::cmp_not_equal(bytesRead, size)) {
        auto error =
            makeError(ErrorCode::SHORT_TRANSFER,
                      std::format("short read: expected {} bytes, received {}",
                                  size, bytesRead));
        error.pid = m_pid;
        error.address = address;
        error.size = size;
        error.bytesTransferred = static_cast<std::size_t>(bytesRead);
        return std::unexpected(std::move(error));
    }

    return result;

#else

    (void)address;
    (void)size;
    return std::unexpected(makeError(ErrorCode::UNSUPPORTED_PLATFORM,
                                     "MemoryReader requires Linux"));

#endif
}
}  // namespace memseek
