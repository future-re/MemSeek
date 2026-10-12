#include "memseek/app/scan_session.hpp"

#include <sys/types.h>

#include <cstddef>
#include <expected>
#include <iterator>
#include <string>
#include <utility>

#include "memseek/memory/change.hpp"
#include "memseek/memory/region.hpp"
#include "memseek/memory/scan.hpp"
#include "memseek/memory/snapshot.hpp"
#include "memseek/value.hpp"

namespace memseek {

ScanSession::ScanSession(const pid_t pid, const std::size_t workerCount)
    : m_pid(pid), m_scanner(workerCount) {}

auto ScanSession::scan(const Value& target, const MemoryScanLevel level,
                       const ScanType type)
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
    MemorySnapshot::Results results;
    for (const auto& region : regions->getRegions()) {
        auto matches = m_scanner.scanRegion(m_pid, region, target, type);
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
