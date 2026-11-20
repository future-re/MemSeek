#pragma once

#include <sys/types.h>

#include <algorithm>
#include <boost/asio/thread_pool.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <thread>
#include <utility>
#include <vector>

#include "memseek/error.hpp"
#include "memseek/memory/chunk.hpp"
#include "memseek/memory/read.hpp"
#include "memseek/memory/reader.hpp"
#include "memseek/memory/region.hpp"
#include "memseek/value.hpp"

namespace memseek {

class ScanResultBuilder;

// A result owns a reference to immutable saved bytes. Copies may share storage,
// but never refer to the input buffer or the target process's live memory.
class ScanResult {
   public:
    explicit ScanResult(std::uintptr_t address = 0,
                        std::span<const std::byte> data = {},
                        std::uint64_t parentRegionId = 0)
        : m_parentRegionId(parentRegionId),
          m_address(address),
          m_data(copyData(data)),
          m_size(data.size()) {}

    [[nodiscard]]
    std::uintptr_t address() const noexcept {
        return m_address;
    }

    [[nodiscard]]
    std::span<const std::byte> data() const noexcept {
        // The default move transfers ownership, leaving an empty source.
        return {m_data.get(), m_data ? m_size : 0};
    }

    [[nodiscard]]
    std::uint64_t parentRegionId() const noexcept {
        return m_parentRegionId;
    }

    bool operator==(const ScanResult& other) const noexcept {
        return m_address == other.m_address &&
               m_parentRegionId == other.m_parentRegionId &&
               std::ranges::equal(data(), other.data());
    }

   private:
    friend class ScanResultBuilder;

    // NOLINTNEXTLINE(modernize-avoid-c-arrays)
    ScanResult(std::uintptr_t address, std::shared_ptr<const std::byte[]> data,
               std::size_t size, std::uint64_t parentRegionId) noexcept
        : m_parentRegionId(parentRegionId),
          m_address(address),
          m_data(std::move(data)),
          m_size(size) {}

    static auto copyData(std::span<const std::byte> data) -> std::shared_ptr<
        const std::byte[]> {  // NOLINT(modernize-avoid-c-arrays)
        if (data.empty()) {
            return {};
        }
        // NOLINTNEXTLINE(modernize-avoid-c-arrays)
        auto storage = std::make_shared_for_overwrite<std::byte[]>(data.size());
        std::memcpy(storage.get(), data.data(), data.size());
        return storage;
    }

    std::uint64_t m_parentRegionId{};
    std::uintptr_t m_address{};
    // NOLINTNEXTLINE(modernize-avoid-c-arrays)
    std::shared_ptr<const std::byte[]> m_data;
    std::size_t m_size{};
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

struct ScanIssue {
    std::uintptr_t address{};
    std::size_t size{};
    MemSeekError error;
};

struct ScanReport {
    std::vector<ScanResult> results;
    std::vector<ScanIssue> issues;
    // Mapping snapshot used for this process scan.
    std::vector<MemoryRegion> regions;

    [[nodiscard]]
    bool complete() const noexcept {
        return issues.empty();
    }
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

    // level.memoryScanStrategy selects strict failure or a partial report.
    // Fatal errors are returned as unexpected in either strategy.
    [[nodiscard]]
    Result<ScanReport> scanProcess(pid_t pid, MemoryScanLevel level,
                                   const Value& target,
                                   ScanType scanType = ScanType::EXACT,
                                   ByteOrder byteOrder = ByteOrder::NATIVE);

    [[nodiscard]]
    Result<ScanReport> scanProcess(pid_t pid, MemoryScanLevel level,
                                   const Value& lowerBound,
                                   const Value& upperBound,
                                   ByteOrder byteOrder = ByteOrder::NATIVE);

    // Both region overloads report local read failures in issues. A successful
    // Result may be incomplete; check report.complete(). Fatal errors still
    // return unexpected. Process-level strict/tolerance policy is not applied.
    [[nodiscard]]
    Result<ScanReport> scanRegion(pid_t pid, const MemoryRegion& region,
                                  const Value& target,
                                  ScanType scanType = ScanType::EXACT,
                                  ByteOrder byteOrder = ByteOrder::NATIVE);

    [[nodiscard]]
    Result<ScanReport> scanRegion(pid_t pid, const MemoryRegion& region,
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
    Result<ScanReport> scanProcessImpl(pid_t pid, MemoryScanLevel level,
                                       const Value& lowerBound,
                                       const Value* upperBound,
                                       ScanType scanType, ByteOrder byteOrder);

    Result<ScanReport> scanRegionImpl(pid_t pid, const MemoryRegion& region,
                                      const Value& lowerBound,
                                      const Value* upperBound,
                                      ScanType scanType, ByteOrder byteOrder);

    static std::vector<ScanResult> scanBufferImpl(const MemoryRead& memory,
                                                  const Value& lowerBound,
                                                  const Value* upperBound,
                                                  ScanType scanType,
                                                  ByteOrder byteOrder);

    static Result<ScanReport> scanChunk(MemoryReader& reader, pid_t pid,
                                        const MemoryRegion& region,
                                        const MemoryChunk& chunk,
                                        const Value& lowerBound,
                                        const Value* upperBound,
                                        ScanType scanType, ByteOrder byteOrder);

    Result<ScanReport> scanChunksParallel(MemoryReader& reader, pid_t pid,
                                          const MemoryRegion& region,
                                          const Value& lowerBound,
                                          const Value* upperBound,
                                          ScanType scanType,
                                          ByteOrder byteOrder);

    boost::asio::thread_pool m_pool;
};

}  // namespace memseek
