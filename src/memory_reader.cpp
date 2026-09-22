#include "memseek/memory_reader.hpp"

#include <sys/uio.h>

#include <cstring>

namespace memseek {

std::expected<MemoryRead, std::string> MemoryReader::read(
    const MemoryChunk& chunk, const MemoryRegion& region,
    const Value& target) const {
#if defined(__linux__)

    if (target.size() == 0) {
        return std::unexpected{"target size must be greater than zero"};
    }

    const auto overlap = target.size() - 1;

    const auto regionEnd = region.start + region.size;

    const auto maxReadable = regionEnd - chunk.address;

    const auto readSize = std::min(chunk.size + overlap, maxReadable);

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

    const auto bytesRead = ::process_vm_readv(m_pid, &local, 1, &remote, 1, 0);

    if (bytesRead < 0) {
        return std::unexpected{std::string{"process_vm_readv failed: "} +
                               std::strerror(errno)};
    }

    result.buffer().resize(static_cast<std::size_t>(bytesRead));

    return result;

#else

    return std::unexpected{"MemoryReader is not supported on this platform"};

#endif
}
}  // namespace memseek