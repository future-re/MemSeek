#include "memseek/memory/snapshot.hpp"

namespace memseek {

auto MemorySnapshot::fromScanResults(const pid_t pid, Results results)
    -> MemorySnapshot {
    return MemorySnapshot{pid, std::move(results)};
}

}  // namespace memseek
