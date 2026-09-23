// Reference scanner for the external-tool comparison harness.
//
// Scans another process with MemoryScanner::scanProcess using the same scope
// as scanmem (all readable+writable regions) and prints wall time and match
// count in a machine-readable form.
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "memseek/memory_scan.hpp"
#include "memseek/value.hpp"

namespace {

std::uint32_t parseValue(const char* text) {
    return static_cast<std::uint32_t>(std::strtoul(text, nullptr, 0));
}

}  // namespace

int main(int argc, char** argv) {
    pid_t pid = 0;
    std::uint32_t value = 0xDEADBEEFu;

    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], "--pid") == 0 && index + 1 < argc) {
            pid = static_cast<pid_t>(std::atoi(argv[++index]));
        } else if (std::strcmp(argv[index], "--value") == 0 &&
                   index + 1 < argc) {
            value = parseValue(argv[++index]);
        }
    }

    if (pid == 0) {
        std::fprintf(stderr, "usage: %s --pid PID [--value VALUE]\n", argv[0]);
        return 1;
    }

    const memseek::Value target(value);
    const memseek::MemoryScanLevel level{};

    memseek::MemoryScanner scanner;

    const auto start = std::chrono::steady_clock::now();
    const auto results = scanner.scanProcess(pid, level, target);
    const auto end = std::chrono::steady_clock::now();

    const auto elapsedMs =
        std::chrono::duration<double, std::milli>(end - start).count();

    std::printf("matches=%zu elapsed_ms=%.3f\n", results.size(), elapsedMs);
    return 0;
}
