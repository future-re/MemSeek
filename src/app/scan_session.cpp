#include "memseek/app/scan_session.hpp"

#include <sys/types.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iterator>
#include <string>
#include <type_traits>
#include <utility>

#include "memseek/memory/change.hpp"
#include "memseek/memory/chunk.hpp"
#include "memseek/memory/reader.hpp"
#include "memseek/memory/region.hpp"
#include "memseek/memory/scan.hpp"
#include "memseek/memory/snapshot.hpp"
#include "memseek/value.hpp"

namespace memseek {

namespace {

enum class RangeValidity : std::uint8_t {
    VALID,
    NON_FINITE,
    REVERSED,
};

template <typename T>
auto validateRangeValues(const Value& lowerBound,
                         const Value& upperBound) -> RangeValidity {
    const auto lower = lowerBound.as<T>();
    const auto upper = upperBound.as<T>();
    if constexpr (std::is_floating_point_v<T>) {
        if (!std::isfinite(lower) || !std::isfinite(upper)) {
            return RangeValidity::NON_FINITE;
        }
    }
    return lower <= upper ? RangeValidity::VALID : RangeValidity::REVERSED;
}

auto validateRangeValues(const Value& lowerBound,
                         const Value& upperBound) -> RangeValidity {
    switch (lowerBound.type()) {
        case ValueType::U_INT8:
            return validateRangeValues<std::uint8_t>(lowerBound, upperBound);
        case ValueType::U_INT16:
            return validateRangeValues<std::uint16_t>(lowerBound, upperBound);
        case ValueType::U_INT32:
            return validateRangeValues<std::uint32_t>(lowerBound, upperBound);
        case ValueType::U_INT64:
            return validateRangeValues<std::uint64_t>(lowerBound, upperBound);
        case ValueType::INT8:
            return validateRangeValues<std::int8_t>(lowerBound, upperBound);
        case ValueType::INT16:
            return validateRangeValues<std::int16_t>(lowerBound, upperBound);
        case ValueType::INT32:
            return validateRangeValues<std::int32_t>(lowerBound, upperBound);
        case ValueType::INT64:
            return validateRangeValues<std::int64_t>(lowerBound, upperBound);
        case ValueType::FLOAT32:
            return validateRangeValues<float>(lowerBound, upperBound);
        case ValueType::FLOAT64:
            return validateRangeValues<double>(lowerBound, upperBound);
        case ValueType::UNKNOWN:
        case ValueType::STR:
        case ValueType::BIN_ARRAY:
            return RangeValidity::REVERSED;
    }
    return RangeValidity::REVERSED;
}

auto validateMemoryAccess(const pid_t pid, const MemoryRegionList& regions)
    -> std::expected<void, std::string> {
    const MemoryReader reader(pid);
    std::string firstError;
    for (const auto& region : regions.getRegions()) {
        if (region.size == 0) {
            continue;
        }
        const MemoryChunk probe{
            .address = region.start,
            .size = 1,
            .parentRegionId = region.id,
        };
        const auto memory = reader.read(probe, region, 0);
        if (memory && memory->size() == 1) {
            return {};
        }
        if (firstError.empty()) {
            firstError = memory ? "process memory probe returned no data"
                                : memory.error();
        }
    }
    if (!regions.empty()) {
        return std::unexpected("failed to read selected process memory: " +
                               firstError);
    }
    return {};
}

}  // namespace

ScanSession::ScanSession(const pid_t pid, const std::size_t workerCount)
    : m_pid(pid), m_scanner(workerCount) {}

auto ScanSession::scan(const Value& target, const MemoryScanLevel level,
                       const ScanType type, const ByteOrder byteOrder)
    -> std::expected<void, std::string> {
    if (target.empty()) {
        return std::unexpected("scan target must not be empty");
    }
    if (type != ScanType::EXACT && type != ScanType::GREATER &&
        type != ScanType::LESS) {
        return std::unexpected("unsupported scan type for a single target");
    }
    if (type != ScanType::EXACT && !target.isNumeric()) {
        return std::unexpected("ordering scans require a numeric target");
    }
    // Read maps here so process errors are not mistaken for zero matches by
    // MemoryScanner::scanProcess().
    const auto regions = readProcess(m_pid, level);
    if (!regions) {
        return std::unexpected(regions.error());
    }
    if (const auto access = validateMemoryAccess(m_pid, *regions); !access) {
        return std::unexpected(access.error());
    }
    MemorySnapshot::Results results;
    for (const auto& region : regions->getRegions()) {
        auto matches =
            m_scanner.scanRegion(m_pid, region, target, type, byteOrder);
        results.insert(results.end(), std::make_move_iterator(matches.begin()),
                       std::make_move_iterator(matches.end()));
    }
    m_snapshot = MemorySnapshot::fromScanResults(m_pid, std::move(results));
    return {};
}

auto ScanSession::scan(const Value& lowerBound, const Value& upperBound,
                       const MemoryScanLevel level, const ByteOrder byteOrder)
    -> std::expected<void, std::string> {
    if (lowerBound.empty() || upperBound.empty()) {
        return std::unexpected("scan bounds must not be empty");
    }
    if (!lowerBound.isNumeric() || !upperBound.isNumeric()) {
        return std::unexpected("range scans require numeric bounds");
    }
    if (lowerBound.type() != upperBound.type()) {
        return std::unexpected("range bounds must have the same type");
    }
    const auto validity = validateRangeValues(lowerBound, upperBound);
    if (validity == RangeValidity::NON_FINITE) {
        return std::unexpected("range bounds must be finite");
    }
    if (validity == RangeValidity::REVERSED) {
        return std::unexpected("range lower bound must not exceed upper bound");
    }

    const auto regions = readProcess(m_pid, level);
    if (!regions) {
        return std::unexpected(regions.error());
    }
    if (const auto access = validateMemoryAccess(m_pid, *regions); !access) {
        return std::unexpected(access.error());
    }

    MemorySnapshot::Results results;
    for (const auto& region : regions->getRegions()) {
        auto matches = m_scanner.scanRegion(m_pid, region, lowerBound,
                                            upperBound, byteOrder);
        results.insert(results.end(), std::make_move_iterator(matches.begin()),
                       std::make_move_iterator(matches.end()));
    }
    m_snapshot = MemorySnapshot::fromScanResults(m_pid, std::move(results));
    return {};
}

auto ScanSession::filter(const ChangeType type)
    -> std::expected<void, std::string> {
    if (!m_snapshot) {
        return std::unexpected("no baseline available; run scan first");
    }
    auto next = captureChanges(*m_snapshot, type);
    if (!next) {
        return std::unexpected(next.error());
    }
    m_snapshot = std::move(*next);
    return {};
}

void ScanSession::setPid(const pid_t pid) noexcept {
    if (m_pid != pid) {
        m_pid = pid;
        reset();
    }
}

void ScanSession::reset() noexcept { m_snapshot.reset(); }

}  // namespace memseek
