#include "memseek/memory/scan.hpp"

#include <algorithm>
#include <boost/asio/post.hpp>
#include <boost/asio/thread_pool.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <stdexcept>
#include <utility>
#include <vector>

#include "memseek/memory/reader.hpp"

namespace memseek {

namespace {

template <typename Matcher>
std::vector<ScanResult> scanWindows(const MemoryRead& memory, std::size_t width,
                                    Matcher matcher) {
    const auto memoryData = memory.data();
    if (width == 0 || memoryData.size() < width) {
        return {};
    }

    std::vector<ScanResult> results;
    const auto lastOffset = memoryData.size() - width;

    for (std::size_t offset = 0; offset <= lastOffset; ++offset) {
        const auto current = memoryData.subspan(offset, width);
        if (matcher(current)) {
            results.emplace_back(memory.address() + offset, current);
        }
    }

    return results;
}

template <ValueNumericType T>
std::vector<ScanResult> scanNumeric(const MemoryRead& memory,
                                    const Value& lowerBound,
                                    const Value* upperBound,
                                    const ScanType scanType) {
    if (lowerBound.type() != getValueType<T>() ||
        lowerBound.size() != sizeof(T)) {
        return {};
    }

    const T lower = lowerBound.as<T>();

    if (scanType == ScanType::RANGE) {
        if (upperBound == nullptr || upperBound->type() != getValueType<T>() ||
            upperBound->size() != sizeof(T)) {
            return {};
        }

        const T upper = upperBound->as<T>();
        if (!(lower <= upper)) {
            return {};
        }

        return scanWindows(
            memory, sizeof(T),
            [lower, upper](const std::span<const std::byte> data) {
                T value{};
                std::memcpy(&value, data.data(), sizeof(value));
                return lower <= value && value <= upper;
            });
    }

    if (scanType == ScanType::GREATER) {
        return scanWindows(memory, sizeof(T),
                           [lower](const std::span<const std::byte> data) {
                               T value{};
                               std::memcpy(&value, data.data(), sizeof(value));
                               return value > lower;
                           });
    }

    if (scanType == ScanType::LESS) {
        return scanWindows(memory, sizeof(T),
                           [lower](const std::span<const std::byte> data) {
                               T value{};
                               std::memcpy(&value, data.data(), sizeof(value));
                               return value < lower;
                           });
    }

    return {};
}

}  // namespace

MemoryScanner::MemoryScanner(std::size_t workerCount)
    : m_pool(std::max<std::size_t>(1, workerCount == 0 ? 1 : workerCount)) {}

MemoryScanner::~MemoryScanner() { m_pool.join(); }

[[nodiscard]]
std::vector<ScanResult> MemoryScanner::scanProcess(pid_t pid,
                                                   MemoryScanLevel level,
                                                   const Value& target,
                                                   ScanType scanType) {
    if (scanType == ScanType::RANGE) {
        throw std::invalid_argument(
            "RANGE scans require both lower and upper bounds");
    }

    return scanProcessImpl(pid, level, target, nullptr, scanType);
}

std::vector<ScanResult> MemoryScanner::scanProcess(pid_t pid,
                                                   MemoryScanLevel level,
                                                   const Value& lowerBound,
                                                   const Value& upperBound) {
    return scanProcessImpl(pid, level, lowerBound, &upperBound,
                           ScanType::RANGE);
}

std::vector<ScanResult> MemoryScanner::scanProcessImpl(pid_t pid,
                                                       MemoryScanLevel level,
                                                       const Value& lowerBound,
                                                       const Value* upperBound,
                                                       ScanType scanType) {
    auto regionList = readProcess(pid, level);
    if (!regionList) {
        return {};
    }

    std::vector<ScanResult> results;
    for (const auto& region : regionList->getRegions()) {
        auto regionResults =
            scanRegionImpl(pid, region, lowerBound, upperBound, scanType);
        if (!regionResults.empty()) {
            results.insert(results.end(), regionResults.begin(),
                           regionResults.end());
        }
    }
    return results;
}

std::vector<ScanResult> MemoryScanner::scanBuffer(const MemoryRead& memory,
                                                  const Value& target,
                                                  ScanType scanType) {
    if (scanType == ScanType::RANGE) {
        throw std::invalid_argument(
            "RANGE scans require both lower and upper bounds");
    }

    return scanBufferImpl(memory, target, nullptr, scanType);
}

std::vector<ScanResult> MemoryScanner::scanBuffer(const MemoryRead& memory,
                                                  const Value& lowerBound,
                                                  const Value& upperBound) {
    return scanBufferImpl(memory, lowerBound, &upperBound, ScanType::RANGE);
}

std::vector<ScanResult> MemoryScanner::scanBufferImpl(const MemoryRead& memory,
                                                      const Value& lowerBound,
                                                      const Value* upperBound,
                                                      ScanType scanType) {
    if (scanType == ScanType::UNKNOWN ||
        (scanType == ScanType::RANGE && upperBound == nullptr) ||
        (scanType != ScanType::RANGE && upperBound != nullptr)) {
        return {};
    }

    if (scanType != ScanType::EXACT && !lowerBound.isNumeric()) {
        return {};
    }

    if (scanType == ScanType::RANGE &&
        (!upperBound->isNumeric() || upperBound->type() != lowerBound.type())) {
        return {};
    }

    switch (scanType) {
        case ScanType::EXACT: {
            const auto targetData = lowerBound.data();
            return scanWindows(
                memory, targetData.size(), [targetData](const auto current) {
                    return std::equal(targetData.begin(), targetData.end(),
                                      current.begin(), current.end());
                });
        }
        case ScanType::RANGE:
        case ScanType::GREATER:
        case ScanType::LESS:
            switch (lowerBound.type()) {
                case ValueType::U_INT8:
                    return scanNumeric<std::uint8_t>(memory, lowerBound,
                                                     upperBound, scanType);
                case ValueType::U_INT16:
                    return scanNumeric<std::uint16_t>(memory, lowerBound,
                                                      upperBound, scanType);
                case ValueType::U_INT32:
                    return scanNumeric<std::uint32_t>(memory, lowerBound,
                                                      upperBound, scanType);
                case ValueType::U_INT64:
                    return scanNumeric<std::uint64_t>(memory, lowerBound,
                                                      upperBound, scanType);
                case ValueType::INT8:
                    return scanNumeric<std::int8_t>(memory, lowerBound,
                                                    upperBound, scanType);
                case ValueType::INT16:
                    return scanNumeric<std::int16_t>(memory, lowerBound,
                                                     upperBound, scanType);
                case ValueType::INT32:
                    return scanNumeric<std::int32_t>(memory, lowerBound,
                                                     upperBound, scanType);
                case ValueType::INT64:
                    return scanNumeric<std::int64_t>(memory, lowerBound,
                                                     upperBound, scanType);
                case ValueType::FLOAT32:
                    return scanNumeric<float>(memory, lowerBound, upperBound,
                                              scanType);
                case ValueType::FLOAT64:
                    return scanNumeric<double>(memory, lowerBound, upperBound,
                                               scanType);
                default:
                    return {};
            }
        case ScanType::UNKNOWN:
            return {};
    }

    return {};
}

std::vector<ScanResult> MemoryScanner::scanRegion(pid_t pid,
                                                  const MemoryRegion& region,
                                                  const Value& target,
                                                  ScanType scanType) {
    if (scanType == ScanType::RANGE) {
        throw std::invalid_argument(
            "RANGE scans require both lower and upper bounds");
    }

    return scanRegionImpl(pid, region, target, nullptr, scanType);
}

std::vector<ScanResult> MemoryScanner::scanRegion(pid_t pid,
                                                  const MemoryRegion& region,
                                                  const Value& lowerBound,
                                                  const Value& upperBound) {
    return scanRegionImpl(pid, region, lowerBound, &upperBound,
                          ScanType::RANGE);
}

std::vector<ScanResult> MemoryScanner::scanRegionImpl(
    pid_t pid, const MemoryRegion& region, const Value& lowerBound,
    const Value* upperBound, ScanType scanType) {
    if (region.size == 0) {
        return {};
    }

    MemoryReader reader(pid);

    const std::size_t chunkCount =
        (region.size + DEFAULT_CHUNK_SIZE - 1) / DEFAULT_CHUNK_SIZE;

    //
    // Small region: avoid thread-pool scheduling overhead.
    //
    if (chunkCount == 1) {
        const auto range = MemoryChunkRange(region);
        const auto chunk = *range.begin();

        std::size_t overlap = lowerBound.size() > 0 ? lowerBound.size() - 1 : 0;

        auto memory = reader.read(chunk, region, overlap);

        if (!memory) {
            return {};
        }

        return scanBufferImpl(*memory, lowerBound, upperBound, scanType);
    }

    //
    // One future per chunk.
    // The chunks themselves are still generated lazily.
    //
    std::vector<std::future<std::vector<ScanResult>>> futures;

    futures.reserve(chunkCount);

    std::size_t overlap = lowerBound.size() > 0 ? lowerBound.size() - 1 : 0;

    for (const auto chunk : MemoryChunkRange(region)) {
        futures.emplace_back(submit([&, chunk]() -> std::vector<ScanResult> {
            auto memory = reader.read(chunk, region, overlap);

            if (!memory) {
                return {};
            }

            auto results =
                scanBufferImpl(*memory, lowerBound, upperBound, scanType);

            //
            // Reader may read overlap bytes so that
            // matches spanning chunk boundaries are
            // detectable.
            //
            // A chunk only owns matches whose starting
            // address lies inside its logical range.
            //
            const auto logicalEnd = chunk.address + chunk.size;

            std::erase_if(results, [logicalEnd](const ScanResult& result) {
                return result.address() >= logicalEnd;
            });

            return results;
        }));
    }

    //
    // Collect per-chunk results.
    //
    std::vector<std::vector<ScanResult>> chunkResults;

    chunkResults.reserve(futures.size());

    std::size_t totalMatches = 0;

    for (auto& future : futures) {
        auto result = future.get();

        totalMatches += result.size();

        chunkResults.emplace_back(std::move(result));
    }

    //
    // Merge once after all workers have finished.
    //
    std::vector<ScanResult> results;
    results.reserve(totalMatches);

    for (auto& chunkResult : chunkResults) {
        results.insert(results.end(),
                       std::make_move_iterator(chunkResult.begin()),
                       std::make_move_iterator(chunkResult.end()));
    }

    return results;
}
}  // namespace memseek
