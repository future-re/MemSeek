#include "memseek/memory_reader.hpp"

#include <sys/uio.h>

#include <cstring>

namespace memseek {

std::expected<MemoryRead, std::string> MemoryReader::read(
    const MemoryChunk& chunk, const Value& target) const {
#if defined(__linux__)

    const auto bufferSize = chunk.size + target.size() - 1;
    std::vector<std::byte> buffer(bufferSize);
    MemoryRead result{chunk.address, chunk.size, buffer};

    iovec local{
        .iov_base = result.buffer().data(),
        .iov_len = bufferSize,
    };

    iovec remote{
        .iov_base = reinterpret_cast<void*>(chunk.address),
        .iov_len = bufferSize,
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