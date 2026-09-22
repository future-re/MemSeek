// Target process for external scanner comparisons.
//
// Allocates one large buffer, fills it with deterministic pseudo-random data,
// plants a few copies of a target value, prints a machine-readable description
// and then waits for a signal. External tools (scanmem, ...) and memseek_scan
// scan this same process, so they see byte-for-byte identical data.
#include <unistd.h>

#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

namespace {

volatile std::sig_atomic_t g_running = 1;

void handleSignal(int /*signal*/) {
    g_running = 0;
}

std::size_t parseSize(const char* text, std::size_t fallback) {
    if (text == nullptr) {
        return fallback;
    }
    return static_cast<std::size_t>(std::strtoull(text, nullptr, 0));
}

std::uint32_t parseValue(const char* text, std::uint32_t fallback) {
    if (text == nullptr) {
        return fallback;
    }
    return static_cast<std::uint32_t>(std::strtoul(text, nullptr, 0));
}

}  // namespace

int main(int argc, char** argv) {
    const char* sizeText = nullptr;
    const char* valueText = nullptr;
    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], "--size") == 0 && index + 1 < argc) {
            sizeText = argv[++index];
        } else if (std::strcmp(argv[index], "--value") == 0 &&
                   index + 1 < argc) {
            valueText = argv[++index];
        }
    }

    const std::size_t size = parseSize(sizeText, 256ull * 1024 * 1024);
    const std::uint32_t value = parseValue(valueText, 0xDEADBEEFu);

    std::vector<std::byte> buffer(size);
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, 255);
    for (auto& byte : buffer) {
        byte = static_cast<std::byte>(dist(rng));
    }

    const std::size_t plantedOffsets[] = {
        std::size_t{8} << 20, size / 2, size - (std::size_t{16} << 20)};
    for (const auto offset : plantedOffsets) {
        std::memcpy(buffer.data() + offset, &value, sizeof(value));
    }

    std::signal(SIGTERM, handleSignal);
    std::signal(SIGINT, handleSignal);

    std::printf(
        "{\"pid\":%d,\"address\":\"%p\",\"size\":%zu,\"value\":%u,"
        "\"planted\":%zu}\n",
        static_cast<int>(getpid()), static_cast<void*>(buffer.data()), size,
        value, sizeof(plantedOffsets) / sizeof(plantedOffsets[0]));
    std::fflush(stdout);

    while (g_running != 0) {
        pause();
    }

    return 0;
}
