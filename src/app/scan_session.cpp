#include "memseek/app/scan_session.hpp"

#include <sys/types.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <type_traits>
#include <utility>

#include "memseek/memory/change.hpp"
#include "memseek/memory/reader.hpp"
#include "memseek/memory/region.hpp"
#include "memseek/memory/scan.hpp"
#include "memseek/memory/snapshot.hpp"
#include "memseek/memory/writer.hpp"
#include "memseek/value.hpp"

namespace memseek {

namespace {

auto contains(const MemoryRegion& region, std::uintptr_t address,
              std::size_t size) -> bool {
    if (address < region.start) {
        return false;
    }
    const auto offset = address - region.start;
    return offset < region.size && size <= region.size - offset;
}

enum class RangeValidity : std::uint8_t {
    VALID,
    NON_FINITE,
    REVERSED,
};

template <typename T>
auto validateRangeValues(const Value& lowerBound, const Value& upperBound)
    -> RangeValidity {
    const auto lower = lowerBound.as<T>();
    const auto upper = upperBound.as<T>();
    if constexpr (std::is_floating_point_v<T>) {
        if (!std::isfinite(lower) || !std::isfinite(upper)) {
            return RangeValidity::NON_FINITE;
        }
    }
    return lower <= upper ? RangeValidity::VALID : RangeValidity::REVERSED;
}

auto validateRangeValues(const Value& lowerBound, const Value& upperBound)
    -> RangeValidity {
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

}  // namespace

ScanSession::ScanSession(const pid_t pid, const std::size_t workerCount)
    : m_pid(pid), m_scanner(workerCount) {}

auto ScanSession::scan(const Value& target, const MemoryScanLevel level,
                       const ScanType type, const ByteOrder byteOrder)
    -> Result<void> {
    if (target.empty()) {
        return std::unexpected(makeError(ErrorCode::INVALID_ARGUMENT,
                                         "scan target must not be empty"));
    }
    if (type != ScanType::EXACT && type != ScanType::GREATER &&
        type != ScanType::LESS) {
        return std::unexpected(
            makeError(ErrorCode::UNSUPPORTED_OPERATION,
                      "unsupported scan type for a single target"));
    }
    if (type != ScanType::EXACT && !target.isNumeric()) {
        return std::unexpected(
            makeError(ErrorCode::INVALID_VALUE,
                      "ordering scans require a numeric target"));
    }
    auto report = m_scanner.scanProcess(m_pid, level, target, type, byteOrder);
    if (!report) {
        return std::unexpected(std::move(report).error());
    }
    m_snapshot = MemorySnapshot(m_pid, std::move(report->results));
    m_regions = std::move(report->regions);
    return {};
}

auto ScanSession::scan(const Value& lowerBound, const Value& upperBound,
                       const MemoryScanLevel level, const ByteOrder byteOrder)
    -> Result<void> {
    if (lowerBound.empty() || upperBound.empty()) {
        return std::unexpected(makeError(ErrorCode::INVALID_ARGUMENT,
                                         "scan bounds must not be empty"));
    }
    if (!lowerBound.isNumeric() || !upperBound.isNumeric()) {
        return std::unexpected(makeError(ErrorCode::INVALID_VALUE,
                                         "range scans require numeric bounds"));
    }
    if (lowerBound.type() != upperBound.type()) {
        return std::unexpected(makeError(
            ErrorCode::INVALID_VALUE, "range bounds must have the same type"));
    }
    const auto validity = validateRangeValues(lowerBound, upperBound);
    if (validity == RangeValidity::NON_FINITE) {
        return std::unexpected(
            makeError(ErrorCode::INVALID_VALUE, "range bounds must be finite"));
    }
    if (validity == RangeValidity::REVERSED) {
        return std::unexpected(
            makeError(ErrorCode::INVALID_VALUE,
                      "range lower bound must not exceed upper bound"));
    }

    auto report =
        m_scanner.scanProcess(m_pid, level, lowerBound, upperBound, byteOrder);
    if (!report) {
        return std::unexpected(std::move(report).error());
    }
    m_snapshot = MemorySnapshot(m_pid, std::move(report->results));
    m_regions = std::move(report->regions);
    return {};
}

auto ScanSession::filter(const ChangeType type) -> Result<void> {
    if (!m_snapshot) {
        return std::unexpected(makeError(
            ErrorCode::NO_BASELINE, "no baseline available; run scan first"));
    }
    auto next = captureChanges(*m_snapshot, type);
    if (!next) {
        return std::unexpected(next.error());
    }
    m_snapshot = std::move(*next);
    return {};
}

auto ScanSession::write(const ScanResult& result,
                        std::span<const std::byte> data)
    -> Result<std::size_t> {
    if (!m_snapshot) {
        return std::unexpected(
            makeError(ErrorCode::NO_BASELINE, "no scan baseline"));
    }
    const auto candidate = std::ranges::find_if(
        m_snapshot->results(),
        [&result](const ScanResult& item) { return &item == &result; });
    if (candidate == m_snapshot->results().end()) {
        return std::unexpected(
            makeError(ErrorCode::INVALID_ARGUMENT,
                      "result is not in the current snapshot"));
    }
    if (candidate->data().empty() || data.empty() ||
        data.size() > candidate->data().size()) {
        return std::unexpected(
            makeError(ErrorCode::INVALID_ARGUMENT, "invalid write size"));
    }
    const auto original =
        std::ranges::find_if(m_regions, [&](const MemoryRegion& region) {
            return contains(region, candidate->address(),
                            candidate->data().size());
        });
    if (original == m_regions.end()) {
        return std::unexpected(makeError(ErrorCode::REGION_MISMATCH,
                                         "original mapping not found"));
    }
    MemoryScanLevel level;
    level.memoryProtection = static_cast<std::uint8_t>(MemoryProtection::WRITE);
    auto regions = readProcess(m_pid, level, false);
    if (!regions) {
        return std::unexpected(regions.error());
    }
    const auto current =
        std::ranges::find_if(*regions, [&](const MemoryRegion& region) {
            return contains(region, candidate->address(),
                            candidate->data().size());
        });
    if (current == regions->end() || current->start != original->start ||
        current->size != original->size) {
        return std::unexpected(makeError(ErrorCode::REGION_MISMATCH,
                                         "mapping changed or is not writable"));
    }
    const auto bytes = MemoryReader(m_pid).read(candidate->address(),
                                                candidate->data().size());
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    if (!std::ranges::equal(*bytes, candidate->data())) {
        return std::unexpected(
            makeError(ErrorCode::VALUE_CHANGED, "value changed since scan"));
    }
    return MemoryWriter(m_pid).write(candidate->address(), data);
}

void ScanSession::setPid(const pid_t pid) noexcept {
    if (m_pid != pid) {
        m_pid = pid;
        reset();
    }
}

void ScanSession::reset() noexcept {
    m_snapshot.reset();
    m_regions.clear();
}

}  // namespace memseek
