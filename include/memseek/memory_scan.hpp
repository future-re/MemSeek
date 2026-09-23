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

#include "memseek/memory_read.hpp"
#include "memseek/memory_region.hpp"
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
    std::vector<ScanResult> scanProcess(pid_t pid, MemoryScanLevel level,
                                        const Value& target);

    [[nodiscard]]
    std::vector<ScanResult> scanRegion(pid_t pid, const MemoryRegion& region,
                                       const Value& target);

    [[nodiscard]]
    static std::vector<ScanResult> scanBuffer(const MemoryRead& memory,
                                              const Value& target);

   private:
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