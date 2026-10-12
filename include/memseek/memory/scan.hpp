#pragma once

#include <sys/types.h>

#include <boost/asio/post.hpp>
#include <boost/asio/thread_pool.hpp>
#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <span>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "memseek/memory/read.hpp"
#include "memseek/memory/region.hpp"
#include "memseek/value.hpp"

namespace memseek {

class ScanResult {
   public:
    explicit ScanResult(std::uintptr_t address = 0,
                        std::span<const std::byte> data = {})
        : m_address(address), m_data(data.begin(), data.end()) {}

    [[nodiscard]]
    std::uintptr_t address() const noexcept {
        return m_address;
    }

    [[nodiscard]]
    std::span<const std::byte> data() const noexcept {
        return m_data;
    }

    bool operator==(const ScanResult&) const = default;

   private:
    std::uintptr_t m_address{};
    std::vector<std::byte> m_data;
};

enum class ScanType : std::uint8_t {
    UNKNOWN,
    EXACT,    // Byte-wise match; supports all Value types.
    RANGE,    // Numeric Value types only; inclusive lower/upper bounds.
    GREATER,  // Numeric Value types only.
    LESS,     // Numeric Value types only.
};

// Byte order of numeric values in the scanned memory. Raw strings and byte
// arrays are always matched verbatim.
enum class ByteOrder : std::uint8_t {
    NATIVE,
    LITTLE,
    BIG,
};

// Numeric Value inputs retain their native representation. byteOrder describes
// the memory being scanned, not the input Value. Results contain raw bytes.
class MemoryScanner {
   public:
    explicit MemoryScanner(
        std::size_t workerCount = std::thread::hardware_concurrency());

    ~MemoryScanner();

    MemoryScanner(const MemoryScanner&) = delete;
    MemoryScanner& operator=(const MemoryScanner&) = delete;

    MemoryScanner(MemoryScanner&&) = delete;
    MemoryScanner& operator=(MemoryScanner&&) = delete;

    [[nodiscard]]
    std::vector<ScanResult> scanProcess(
        pid_t pid, MemoryScanLevel level, const Value& target,
        ScanType scanType = ScanType::EXACT,
        ByteOrder byteOrder = ByteOrder::NATIVE);

    [[nodiscard]]
    std::vector<ScanResult> scanProcess(
        pid_t pid, MemoryScanLevel level, const Value& lowerBound,
        const Value& upperBound, ByteOrder byteOrder = ByteOrder::NATIVE);

    [[nodiscard]]
    std::vector<ScanResult> scanRegion(pid_t pid, const MemoryRegion& region,
                                       const Value& target,
                                       ScanType scanType = ScanType::EXACT,
                                       ByteOrder byteOrder = ByteOrder::NATIVE);

    [[nodiscard]]
    std::vector<ScanResult> scanRegion(pid_t pid, const MemoryRegion& region,
                                       const Value& lowerBound,
                                       const Value& upperBound,
                                       ByteOrder byteOrder = ByteOrder::NATIVE);

    // RANGE, GREATER, and LESS require Value::isNumeric() to be true.
    // EXACT remains available for numeric values, strings, and byte arrays.
    [[nodiscard]]
    static std::vector<ScanResult> scanBuffer(
        const MemoryRead& memory, const Value& target,
        ScanType scanType = ScanType::EXACT,
        ByteOrder byteOrder = ByteOrder::NATIVE);

    // Range scans use an inclusive [lowerBound, upperBound] interval.
    [[nodiscard]]
    static std::vector<ScanResult> scanBuffer(
        const MemoryRead& memory, const Value& lowerBound,
        const Value& upperBound, ByteOrder byteOrder = ByteOrder::NATIVE);

   private:
    std::vector<ScanResult> scanProcessImpl(pid_t pid, MemoryScanLevel level,
                                            const Value& lowerBound,
                                            const Value* upperBound,
                                            ScanType scanType,
                                            ByteOrder byteOrder);

    std::vector<ScanResult> scanRegionImpl(
        pid_t pid, const MemoryRegion& region, const Value& lowerBound,
        const Value* upperBound, ScanType scanType, ByteOrder byteOrder);

    static std::vector<ScanResult> scanBufferImpl(const MemoryRead& memory,
                                                  const Value& lowerBound,
                                                  const Value* upperBound,
                                                  ScanType scanType,
                                                  ByteOrder byteOrder);

    template <typename Function>
    auto submit(Function&& function)
        -> std::future<std::invoke_result_t<std::decay_t<Function>&>>;

    boost::asio::thread_pool m_pool;
};

template <typename Function>
auto MemoryScanner::submit(Function&& function)
    -> std::future<std::invoke_result_t<std::decay_t<Function>&>> {
    using Task = std::decay_t<Function>;
    using Result = std::invoke_result_t<Task&>;

    auto task = std::make_shared<std::packaged_task<Result()>>(
        std::forward<Function>(function));

    auto future = task->get_future();

    boost::asio::post(m_pool, [task] { (*task)(); });

    return future;
}

}  // namespace memseek
