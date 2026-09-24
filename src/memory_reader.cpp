#include "memseek/memory_reader.hpp"

#include <fcntl.h>
#include <sys/uio.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>
#include <format>
#include <limits>

namespace {

auto readProcMem(const pid_t pid, const std::uintptr_t address,
                 std::byte* buffer, const std::size_t size)
    -> std::expected<std::size_t, std::string> {
    const auto path = std::format("/proc/{}/mem", pid);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    const int descriptor = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) {
        return std::unexpected(
            std::format("open {} failed: {}", path, std::strerror(errno)));
    }

    std::size_t total = 0;
    while (total < size) {
        const auto count = ::pread(descriptor, buffer + total, size - total,
                                   static_cast<off_t>(address + total));
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            const auto error =
                std::format("pread {} failed: {}", path, std::strerror(errno));
            ::close(descriptor);
            return std::unexpected(error);
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

std::expected<MemoryRead, std::string> MemoryReader::read(
    const MemoryChunk& chunk, const MemoryRegion& region,
    const Value& target) const {
#if defined(__linux__)

    if (target.size() == 0) {
        return std::unexpected{"target size must be greater than zero"};
    }

    const auto overlap = target.size() - 1;

    if (region.size >
        std::numeric_limits<std::uintptr_t>::max() - region.start) {
        return std::unexpected{"memory region address range overflows"};
    }

    const auto regionEnd = region.start + region.size;

    if (chunk.size == 0 || chunk.address < region.start ||
        chunk.address > regionEnd || chunk.size > regionEnd - chunk.address) {
        return std::unexpected{"memory chunk is outside the region"};
    }

    const auto maxReadable = regionEnd - chunk.address;
    const auto requestedSize =
        chunk.size > std::numeric_limits<std::size_t>::max() - overlap
            ? std::numeric_limits<std::size_t>::max()
            : chunk.size + overlap;
    const auto readSize = std::min(requestedSize, maxReadable);

    MemoryRead result{chunk.address, chunk.size,
                      std::vector<std::byte>(readSize)};

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
        const auto processVmError =
            std::string{"process_vm_readv failed: "} + std::strerror(errno);
        const auto fallback =
            readProcMem(m_pid, chunk.address, result.buffer().data(), readSize);
        if (!fallback) {
            return std::unexpected{processVmError + "; " + fallback.error()};
        }
        bytesRead = static_cast<ssize_t>(*fallback);
    }

    result.buffer().resize(static_cast<std::size_t>(bytesRead));

    return result;

#else

    return std::unexpected{"MemoryReader is not supported on this platform"};

#endif
}
}  // namespace memseek
