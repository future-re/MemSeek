#include "memseek/memory/scan.hpp"

#include <sys/types.h>

#include <algorithm>
#include <bit>
#include <boost/asio/thread_pool.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <format>
#include <future>
#include <iterator>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include "memseek/memory/chunk.hpp"
#include "memseek/memory/read.hpp"
#include "memseek/memory/reader.hpp"
#include "memseek/memory/region.hpp"
#include "memseek/value.hpp"

namespace memseek {

namespace {

auto needsByteSwap(const ByteOrder byteOrder) -> bool {
    static_assert(std::endian::native == std::endian::little ||
                  std::endian::native == std::endian::big);
    switch (byteOrder) {
        case ByteOrder::NATIVE:
            return false;
        case ByteOrder::LITTLE:
            return std::endian::native != std::endian::little;
        case ByteOrder::BIG:
            return std::endian::native != std::endian::big;
    }
    throw std::invalid_argument("invalid byte order");
}

template <ValueNumericType T>
auto decodeNumeric(std::span<const std::byte> data, bool swap) -> T {
    T value{};
    std::memcpy(&value, data.data(), sizeof(value));
    if (swap) {
        using Bits = std::conditional_t<
            sizeof(T) == 1, std::uint8_t,
            std::conditional_t<sizeof(T) == 2, std::uint16_t,
                               std::conditional_t<sizeof(T) == 4, std::uint32_t,
                                                  std::uint64_t>>>;
        value = std::bit_cast<T>(std::byteswap(std::bit_cast<Bits>(value)));
    }
    return value;
}

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
            results.emplace_back(memory.address() + offset, current,
                                 memory.parentRegionId());
        }
    }

    return results;
}

auto chunkReadSize(const MemoryChunk& chunk, const MemoryRegion& region,
                   const std::size_t overlap) -> std::size_t {
    const auto offset = chunk.address - region.start;
    const auto remaining = region.size - offset;
    const auto extra = std::min(overlap, remaining - chunk.size);
    return chunk.size + extra;
}

auto withChunkContext(MemSeekError error, const pid_t pid,
                      const MemoryChunk& chunk) -> MemSeekError {
    error.message =
        std::format("memory range [{:#x}, {:#x}): {}", chunk.address,
                    chunk.address + chunk.size, error.message);
    error.pid = pid;
    error.address = chunk.address;
    if (!error.size) {
        error.size = chunk.size;
    }
    return error;
}

template <ValueNumericType T>
std::vector<ScanResult> scanNumeric(const MemoryRead& memory,
                                    const Value& lowerBound,
                                    const Value* upperBound,
                                    const ScanType scanType,
                                    const ByteOrder byteOrder) {
    if (lowerBound.type() != getValueType<T>() ||
        lowerBound.size() != sizeof(T)) {
        return {};
    }

    const T lower = lowerBound.as<T>();
    const bool swap = needsByteSwap(byteOrder);

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
            [lower, upper, swap](const std::span<const std::byte> data) {
                const T value = decodeNumeric<T>(data, swap);
                return lower <= value && value <= upper;
            });
    }

    if (scanType == ScanType::GREATER) {
        return scanWindows(
            memory, sizeof(T),
            [lower, swap](const std::span<const std::byte> data) {
                const T value = decodeNumeric<T>(data, swap);
                return value > lower;
            });
    }

    if (scanType == ScanType::LESS) {
        return scanWindows(
            memory, sizeof(T),
            [lower, swap](const std::span<const std::byte> data) {
                const T value = decodeNumeric<T>(data, swap);
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
MemoryScanner::ScanOutcome MemoryScanner::scanProcess(pid_t pid,
                                                      MemoryScanLevel level,
                                                      const Value& target,
                                                      ScanType scanType,
                                                      ByteOrder byteOrder) {
    if (scanType == ScanType::RANGE) {
        throw std::invalid_argument(
            "RANGE scans require both lower and upper bounds");
    }

    return scanProcessImpl(pid, level, target, nullptr, scanType, byteOrder);
}

MemoryScanner::ScanOutcome MemoryScanner::scanProcess(pid_t pid,
                                                      MemoryScanLevel level,
                                                      const Value& lowerBound,
                                                      const Value& upperBound,
                                                      ByteOrder byteOrder) {
    return scanProcessImpl(pid, level, lowerBound, &upperBound, ScanType::RANGE,
                           byteOrder);
}

MemoryScanner::ScanOutcome MemoryScanner::scanProcessImpl(
    pid_t pid, MemoryScanLevel level, const Value& lowerBound,
    const Value* upperBound, ScanType scanType, ByteOrder byteOrder) {
    auto regionList = readProcess(pid, level);
    if (!regionList) {
        auto error = regionList.error();
        error.message =
            "failed to enumerate process mappings: " + error.message;
        error.pid = pid;
        return std::unexpected(std::move(error));
    }

    std::vector<ScanResult> results;
    for (const auto& region : regionList->getRegions()) {
        auto regionResults = scanRegionImpl(pid, region, lowerBound, upperBound,
                                            scanType, byteOrder);
        if (!regionResults) {
            return std::unexpected(regionResults.error());
        }
        results.insert(results.end(),
                       std::make_move_iterator(regionResults->begin()),
                       std::make_move_iterator(regionResults->end()));
    }
    return results;
}

std::vector<ScanResult> MemoryScanner::scanBuffer(const MemoryRead& memory,
                                                  const Value& target,
                                                  ScanType scanType,
                                                  ByteOrder byteOrder) {
    if (scanType == ScanType::RANGE) {
        throw std::invalid_argument(
            "RANGE scans require both lower and upper bounds");
    }

    return scanBufferImpl(memory, target, nullptr, scanType, byteOrder);
}

std::vector<ScanResult> MemoryScanner::scanBuffer(const MemoryRead& memory,
                                                  const Value& lowerBound,
                                                  const Value& upperBound,
                                                  ByteOrder byteOrder) {
    return scanBufferImpl(memory, lowerBound, &upperBound, ScanType::RANGE,
                          byteOrder);
}

std::vector<ScanResult> MemoryScanner::scanBufferImpl(const MemoryRead& memory,
                                                      const Value& lowerBound,
                                                      const Value* upperBound,
                                                      ScanType scanType,
                                                      ByteOrder byteOrder) {
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
            auto targetData = lowerBound.data();
            std::vector<std::byte> encoded;
            if (lowerBound.isNumeric() && needsByteSwap(byteOrder)) {
                encoded.assign(targetData.begin(), targetData.end());
                std::ranges::reverse(encoded);
                targetData = encoded;
            }
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
                    return scanNumeric<std::uint8_t>(
                        memory, lowerBound, upperBound, scanType, byteOrder);
                case ValueType::U_INT16:
                    return scanNumeric<std::uint16_t>(
                        memory, lowerBound, upperBound, scanType, byteOrder);
                case ValueType::U_INT32:
                    return scanNumeric<std::uint32_t>(
                        memory, lowerBound, upperBound, scanType, byteOrder);
                case ValueType::U_INT64:
                    return scanNumeric<std::uint64_t>(
                        memory, lowerBound, upperBound, scanType, byteOrder);
                case ValueType::INT8:
                    return scanNumeric<std::int8_t>(
                        memory, lowerBound, upperBound, scanType, byteOrder);
                case ValueType::INT16:
                    return scanNumeric<std::int16_t>(
                        memory, lowerBound, upperBound, scanType, byteOrder);
                case ValueType::INT32:
                    return scanNumeric<std::int32_t>(
                        memory, lowerBound, upperBound, scanType, byteOrder);
                case ValueType::INT64:
                    return scanNumeric<std::int64_t>(
                        memory, lowerBound, upperBound, scanType, byteOrder);
                case ValueType::FLOAT32:
                    return scanNumeric<float>(memory, lowerBound, upperBound,
                                              scanType, byteOrder);
                case ValueType::FLOAT64:
                    return scanNumeric<double>(memory, lowerBound, upperBound,
                                               scanType, byteOrder);
                default:
                    return {};
            }
        case ScanType::UNKNOWN:
            return {};
    }

    return {};
}

MemoryScanner::ScanOutcome MemoryScanner::scanRegion(pid_t pid,
                                                     const MemoryRegion& region,
                                                     const Value& target,
                                                     ScanType scanType,
                                                     ByteOrder byteOrder) {
    if (scanType == ScanType::RANGE) {
        throw std::invalid_argument(
            "RANGE scans require both lower and upper bounds");
    }

    return scanRegionImpl(pid, region, target, nullptr, scanType, byteOrder);
}

MemoryScanner::ScanOutcome MemoryScanner::scanRegion(pid_t pid,
                                                     const MemoryRegion& region,
                                                     const Value& lowerBound,
                                                     const Value& upperBound,
                                                     ByteOrder byteOrder) {
    return scanRegionImpl(pid, region, lowerBound, &upperBound, ScanType::RANGE,
                          byteOrder);
}

MemoryScanner::ScanOutcome MemoryScanner::scanRegionImpl(
    pid_t pid, const MemoryRegion& region, const Value& lowerBound,
    const Value* upperBound, ScanType scanType, ByteOrder byteOrder) {
    if (region.size == 0) {
        return {};
    }

    if (region.size >
        std::numeric_limits<std::uintptr_t>::max() - region.start) {
        auto error = makeError(ErrorCode::ADDRESS_OUT_OF_RANGE,
                               "memory region address range overflows");

        error.pid = pid;
        error.address = region.start;
        error.size = region.size;

        return std::unexpected(std::move(error));
    }

    MemoryReader reader(pid);

    const std::size_t chunkCount = ((region.size - 1) / DEFAULT_CHUNK_SIZE) + 1;

    if (chunkCount == 1) {
        const auto chunk = *MemoryChunkRange(region).begin();

        return scanChunk(reader, pid, region, chunk, lowerBound, upperBound,
                         scanType, byteOrder);
    }

    return scanChunksParallel(reader, pid, region, lowerBound, upperBound,
                              scanType, byteOrder);
}

MemoryScanner::ScanOutcome MemoryScanner::scanChunk(
    MemoryReader& reader, pid_t pid, const MemoryRegion& region,
    const MemoryChunk& chunk, const Value& lowerBound, const Value* upperBound,
    ScanType scanType, ByteOrder byteOrder) {
    const std::size_t overlap = lowerBound.empty() ? 0 : lowerBound.size() - 1;

    auto bytes =
        reader.read(chunk.address, chunkReadSize(chunk, region, overlap));

    if (!bytes) {
        return std::unexpected(withChunkContext(bytes.error(), pid, chunk));
    }

    const MemoryRead memory{chunk.address, chunk.size, std::move(*bytes),
                            chunk.parentRegionId};

    auto results =
        scanBufferImpl(memory, lowerBound, upperBound, scanType, byteOrder);

    const auto logicalEnd = chunk.address + chunk.size;

    std::erase_if(results, [logicalEnd](const ScanResult& result) {
        return result.address() >= logicalEnd;
    });

    return results;
}

MemoryScanner::ScanOutcome MemoryScanner::getChunkOutcome(
    std::future<ScanOutcome>& future, pid_t pid, const MemoryChunk& chunk) {
    try {
        return future.get();
    } catch (const std::exception& e) {
        auto error = makeError(ErrorCode::MEMORY_READ_FAILED,
                               std::format("scan worker failed: {}", e.what()));

        return std::unexpected(withChunkContext(std::move(error), pid, chunk));
    } catch (...) {
        auto error = makeError(ErrorCode::MEMORY_READ_FAILED,
                               "scan worker failed with unknown error");

        return std::unexpected(withChunkContext(std::move(error), pid, chunk));
    }
}

MemoryScanner::ScanOutcome MemoryScanner::collectChunkResults(
    std::vector<std::future<ScanOutcome>>& futures,
    const std::vector<MemoryChunk>& chunks, pid_t pid) {
    std::vector<std::vector<ScanResult>> chunkResults;
    chunkResults.reserve(futures.size());

    std::size_t totalMatches = 0;
    std::optional<MemSeekError> firstError;

    for (std::size_t index = 0; index < futures.size(); ++index) {
        auto outcome =
            getChunkOutcome(futures[index], pid, chunks[index]);  // NOLINT

        if (!outcome) {
            if (!firstError) {
                firstError = std::move(outcome).error();
            }

            continue;
        }

        totalMatches += outcome->size();
        chunkResults.push_back(std::move(*outcome));
    }

    if (firstError) {
        return std::unexpected(std::move(*firstError));
    }

    std::vector<ScanResult> results;
    results.reserve(totalMatches);

    for (auto& chunkResult : chunkResults) {
        results.insert(results.end(),
                       std::make_move_iterator(chunkResult.begin()),
                       std::make_move_iterator(chunkResult.end()));
    }

    return results;
}

MemoryScanner::ScanOutcome MemoryScanner::scanChunksParallel(
    MemoryReader& reader, pid_t pid, const MemoryRegion& region,
    const Value& lowerBound, const Value* upperBound, ScanType scanType,
    ByteOrder byteOrder) {
    const std::size_t chunkCount = ((region.size - 1) / DEFAULT_CHUNK_SIZE) + 1;

    std::vector<std::future<ScanOutcome>> futures;
    std::vector<MemoryChunk> chunks;

    futures.reserve(chunkCount);
    chunks.reserve(chunkCount);

    for (const auto& chunk : MemoryChunkRange(region)) {
        chunks.push_back(chunk);

        futures.emplace_back(submit([&, chunk]() -> ScanOutcome {
            try {
                return scanChunk(reader, pid, region, chunk, lowerBound,
                                 upperBound, scanType, byteOrder);
            } catch (const std::exception& e) {
                auto error =
                    makeError(ErrorCode::MEMORY_READ_FAILED,
                              std::format("scan worker failed: {}", e.what()));

                return std::unexpected(
                    withChunkContext(std::move(error), pid, chunk));
            } catch (...) {
                auto error = makeError(ErrorCode::MEMORY_READ_FAILED,
                                       "scan worker failed with unknown error");

                return std::unexpected(
                    withChunkContext(std::move(error), pid, chunk));
            }
        }));
    }

    return collectChunkResults(futures, chunks, pid);
}
}  // namespace memseek
