#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <vector>
namespace memseek {
enum class MemoryProtection : std::uint8_t {
    NONE = 0,
    READ = 1 << 0,
    WRITE = 1 << 1,
    EXECUTE = 1 << 2,
};

constexpr auto operator|(MemoryProtection lhs,
                         MemoryProtection rhs) noexcept -> MemoryProtection {
    using T = std::underlying_type_t<MemoryProtection>;

    return static_cast<MemoryProtection>(static_cast<T>(lhs) |
                                         static_cast<T>(rhs));
}

constexpr auto operator|=(MemoryProtection& lhs,
                          MemoryProtection rhs) noexcept -> MemoryProtection& {
    lhs = lhs | rhs;
    return lhs;
}

enum class MemoryRegionType : std::uint8_t {
    UNKNOWN = 0,
    HEAP = 1 << 0,
    STACK = 1 << 1,
    ANONYMOUS = 1 << 2,
    MODULE = 1 << 3,
};

constexpr auto operator|(MemoryRegionType lhs,
                         MemoryRegionType rhs) noexcept -> MemoryRegionType {
    using T = std::underlying_type_t<MemoryRegionType>;

    return static_cast<MemoryRegionType>(static_cast<T>(lhs) |
                                         static_cast<T>(rhs));
}

constexpr auto operator|=(MemoryRegionType& lhs,
                          MemoryRegionType rhs) noexcept -> MemoryRegionType& {
    lhs = lhs | rhs;
    return lhs;
}

struct MemoryScanLevel {
    uint8_t memoryProtection{static_cast<std::uint8_t>(
        MemoryProtection::READ | MemoryProtection::WRITE)};
    uint8_t memoryRegionType{
        static_cast<std::uint8_t>(MemoryRegionType::HEAP) |
        static_cast<std::uint8_t>(MemoryRegionType::STACK) |
        static_cast<std::uint8_t>(MemoryRegionType::ANONYMOUS) |
        static_cast<std::uint8_t>(MemoryRegionType::MODULE)};
};

struct MemoryRegion {
    uint64_t id{0};
    uintptr_t start{};
    std::size_t size{};
    MemoryProtection protection{MemoryProtection::NONE};
    MemoryRegionType regionType{MemoryRegionType::UNKNOWN};
    std::string pathname;
};

class MemoryRegionList {
   private:
    std::vector<MemoryRegion> m_regions;

   public:
    [[nodiscard]]
    auto getRegions() const -> const std::vector<MemoryRegion>& {
        return m_regions;
    }

    auto addRegion(MemoryRegion region) -> void {
        m_regions.push_back(std::move(region));
    }

    auto clear() noexcept -> void { m_regions.clear(); }

    [[nodiscard]]
    auto size() const noexcept -> std::size_t {
        return m_regions.size();
    }

    [[nodiscard]]
    auto empty() const noexcept -> bool {
        return m_regions.empty();
    }
};

auto readProcess(pid_t pid, MemoryScanLevel level = MemoryScanLevel{})
    -> std::expected<MemoryRegionList, std::string>;

}  // namespace memseek