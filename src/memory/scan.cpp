#include "memseek/memory/scan.hpp"

#include <sys/types.h>

#include <algorithm>
#include <bit>
#include <boost/asio/post.hpp>
#include <boost/asio/thread_pool.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <format>
#include <future>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <system_error>
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

void appendReport(ScanReport& destination, ScanReport&& source) {
    destination.results.insert(destination.results.end(),
                               std::make_move_iterator(source.results.begin()),
                               std::make_move_iterator(source.results.end()));
    destination.issues.insert(destination.issues.end(),
                              std::make_move_iterator(source.issues.begin()),
                              std::make_move_iterator(source.issues.end()));
}

auto isLocalReadFailure(const MemSeekError& error) -> bool {
    // Worker exceptions carry no system error and must remain fatal. Only
    // known local memory failures can be represented as gaps in the report.
    if (error.code == ErrorCode::SHORT_TRANSFER) {
        return true;
    }
    return error.code == ErrorCode::MEMORY_READ_FAILED &&
           (error.systemError == std::errc::bad_address ||
            error.systemError == std::errc::io_error);
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
Result<ScanReport> MemoryScanner::scanProcess(pid_t pid, MemoryScanLevel level,
                                              const Value& target,
                                              ScanType scanType,
                                              ByteOrder byteOrder) {
    if (scanType == ScanType::RANGE) {
        throw std::invalid_argument(
            "RANGE scans require both lower and upper bounds");
    }

    return scanProcessImpl(pid, level, target, nullptr, scanType, byteOrder);
}

Result<ScanReport> MemoryScanner::scanProcess(pid_t pid, MemoryScanLevel level,
                                              const Value& lowerBound,
                                              const Value& upperBound,
                                              ByteOrder byteOrder) {
    return scanProcessImpl(pid, level, lowerBound, &upperBound, ScanType::RANGE,
                           byteOrder);
}

Result<ScanReport> MemoryScanner::scanProcessImpl(
    pid_t pid, MemoryScanLevel level, const Value& lowerBound,
    const Value* upperBound, ScanType scanType, ByteOrder byteOrder) {
    if (level.memoryScanStrategy != ScanStrategy::STRICT &&
        level.memoryScanStrategy != ScanStrategy::TOLERANCE) {
        auto error = makeError(ErrorCode::INVALID_ARGUMENT,
                               "invalid memory scan strategy");
        error.pid = pid;
        return std::unexpected(std::move(error));
    }

    auto regionList = readProcess(pid, level);
    if (!regionList) {
        auto error = regionList.error();
        error.message =
            "failed to enumerate process mappings: " + error.message;
        error.pid = pid;
        return std::unexpected(std::move(error));
    }

    ScanReport processReport;
    for (const auto& region : *regionList) {
        auto regionResults = scanRegionImpl(pid, region, lowerBound, upperBound,
                                            scanType, byteOrder);
        if (!regionResults) {
            return std::unexpected(regionResults.error());
        }
        if (level.memoryScanStrategy == ScanStrategy::STRICT &&
            !regionResults->complete()) {
            return std::unexpected(
                std::move(regionResults->issues.front().error));
        }
        appendReport(processReport, std::move(*regionResults));
    }
    processReport.regions = std::move(*regionList);
    return processReport;
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

Result<ScanReport> MemoryScanner::scanRegion(pid_t pid,
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

Result<ScanReport> MemoryScanner::scanRegion(pid_t pid,
                                             const MemoryRegion& region,
                                             const Value& lowerBound,
                                             const Value& upperBound,
                                             ByteOrder byteOrder) {
    return scanRegionImpl(pid, region, lowerBound, &upperBound, ScanType::RANGE,
                          byteOrder);
}

Result<ScanReport> MemoryScanner::scanRegionImpl(
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

Result<ScanReport> MemoryScanner::scanChunk(
    MemoryReader& reader, pid_t pid, const MemoryRegion& region,
    const MemoryChunk& chunk, const Value& lowerBound, const Value* upperBound,
    ScanType scanType, ByteOrder byteOrder) {
    try {
        const std::size_t overlap =
            lowerBound.empty() ? 0 : lowerBound.size() - 1;
        auto bytes =
            reader.read(chunk.address, chunkReadSize(chunk, region, overlap));

        ScanReport report;
        if (!bytes) {
            auto error = std::move(bytes).error();
            if (!isLocalReadFailure(error)) {
                return std::unexpected(std::move(error));
            }
            report.issues.push_back({
                .address = chunk.address,
                .size = chunk.size,
                .error = std::move(error),
            });
            return report;
        }

        const MemoryRead memory{chunk.address, std::move(*bytes),
                                chunk.parentRegionId};
        report.results =
            scanBufferImpl(memory, lowerBound, upperBound, scanType, byteOrder);

        const auto logicalEnd = chunk.address + chunk.size;
        std::erase_if(report.results, [logicalEnd](const ScanResult& result) {
            return result.address() >= logicalEnd;
        });
        return report;
    } catch (const std::exception& e) {
        auto error = makeError(ErrorCode::MEMORY_READ_FAILED,
                               std::format("scan chunk failed: {}", e.what()));
        error.pid = pid;
        error.address = chunk.address;
        error.size = chunk.size;
        return std::unexpected(std::move(error));
    } catch (...) {
        auto error = makeError(ErrorCode::MEMORY_READ_FAILED,
                               "scan chunk failed with unknown error");
        error.pid = pid;
        error.address = chunk.address;
        error.size = chunk.size;
        return std::unexpected(std::move(error));
    }
}

Result<ScanReport> MemoryScanner::scanChunksParallel(
    MemoryReader& reader, pid_t pid, const MemoryRegion& region,
    const Value& lowerBound, const Value* upperBound, ScanType scanType,
    ByteOrder byteOrder) {
    const std::size_t chunkCount = ((region.size - 1) / DEFAULT_CHUNK_SIZE) + 1;

    std::vector<std::future<Result<ScanReport>>> futures;
    futures.reserve(chunkCount);
    try {
        for (const auto& chunk : MemoryChunkRange(region)) {
            auto task =
                std::make_shared<std::packaged_task<Result<ScanReport>()>>(
                    [&, chunk] {
                        return scanChunk(reader, pid, region, chunk, lowerBound,
                                         upperBound, scanType, byteOrder);
                    });
            futures.push_back(task->get_future());
            boost::asio::post(m_pool, [task] { (*task)(); });
        }

        ScanReport report;
        std::optional<MemSeekError> firstError;
        std::size_t index = 0;
        for (const auto& chunk : MemoryChunkRange(region)) {
            Result<ScanReport> outcome;
            try {
                outcome = futures.at(index++).get();
            } catch (const std::exception& e) {
                auto error =
                    makeError(ErrorCode::MEMORY_READ_FAILED,
                              std::format("scan worker failed: {}", e.what()));
                error.pid = pid;
                error.address = chunk.address;
                error.size = chunk.size;
                outcome = std::unexpected(std::move(error));
            }
            if (!outcome) {
                if (!firstError) {
                    firstError = std::move(outcome).error();
                }
                continue;
            }
            appendReport(report, std::move(*outcome));
        }
        if (firstError) {
            return std::unexpected(std::move(*firstError));
        }
        return report;
    } catch (...) {
        // Tasks hold references to this scan. Finish them before unwinding.
        for (auto& future : futures) {
            if (future.valid()) {
                future.wait();
            }
        }
        throw;
    }
}
}  // namespace memseek
