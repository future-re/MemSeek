#include "memseek/memory/writer.hpp"

#include <sys/uio.h>

#include <cerrno>
#include <cstring>
#include <format>
#include <limits>

namespace memseek {

MemoryWriter::MemoryWriter(pid_t pid) noexcept : m_pid{pid} {}

auto MemoryWriter::write(const MemoryWriteTarget& target,
                         std::span<const std::byte> data)
    -> std::expected<std::size_t, std::string> {
#ifdef __linux__
    if (data.size() > target.size) {
        return std::unexpected("write data exceeds target size");
    }
    if (data.empty()) {
        return std::size_t{0};
    }
    if (target.address >
        std::numeric_limits<std::uintptr_t>::max() - data.size()) {
        return std::unexpected("write address range overflows");
    }

    std::size_t totalWritten = 0;
    while (totalWritten < data.size()) {
        iovec local{
            .iov_base = const_cast<std::byte*>(data.data() + totalWritten), // NOLINT
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
            return std::unexpected(std::format("process_vm_writev failed: {}",
                                               std::strerror(errno)));
        }
        if (written == 0) {
            return std::unexpected(
                "process_vm_writev returned zero bytes written");
        }

        totalWritten += static_cast<std::size_t>(written);
    }

    return totalWritten;
#else
    (void)target;
    (void)data;
    return std::unexpected{"MemoryWriter is not supported on this platform"};
#endif
}

}  // namespace memseek