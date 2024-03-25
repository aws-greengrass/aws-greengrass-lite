#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <tuple>

namespace util {
    /**
     * Create a simple 1:1 lookup table (e.g. Symbols to Enums)
     */
    template<typename VT1, typename VT2, uint32_t Nm>
    class LookupTable {
    public:
        using source_type = VT1;
        using mapped_type = VT2;

    private:
        std::array<VT1, Nm> _first;
        std::array<VT2, Nm> _second;

        template<size_t... Is, typename Tuple>
        constexpr LookupTable(std::index_sequence<Is...>, Tuple args) noexcept
            : _first{std::move(std::get<Is * 2>(args))...},
              _second{std::move(std::get<Is * 2 + 1>(args))...} {
        }

        constexpr auto find(const VT1 &v) const noexcept ->
            typename std::array<VT1, Nm>::const_iterator {
            auto first = std::begin(_first);
            const auto last = std::end(_first);
            while((first != last) && (*first != v)) {
                ++first;
            }
            return first;
        }

        constexpr auto rfind(const VT2 &v) const noexcept ->
            typename std::array<VT2, Nm>::const_iterator {
            auto first = std::begin(_second);
            const auto last = std::end(_second);
            while((first != last) && (*first != v)) {
                ++first;
            }
            return first;
        }

    public:
        [[nodiscard]] constexpr uint32_t size() const noexcept {
            return Nm;
        }

        [[nodiscard]] constexpr uint32_t max_size() const noexcept {
            return Nm;
        }

        template<typename... Ts>
        constexpr explicit LookupTable(const Ts &...args) noexcept
            : LookupTable{std::make_index_sequence<sizeof...(Ts) / 2>{}, std::tie(args...)} {
            static_assert(sizeof...(Ts) % 2 == 0, "LookupTable must consist of kv-pairs");
            static_assert(sizeof...(Ts) / 2 == Nm, "Size mismatch");
        }

        [[nodiscard]] constexpr std::optional<VT2> lookup(const VT1 &v) const noexcept {
            if(auto i = indexOf(v); i.has_value()) {
                return _second.at(i.value());
            }
            return {};
        }

        [[nodiscard]] constexpr std::optional<VT1> rlookup(const VT2 &v) const noexcept {
            if(auto i = rindexOf(v); i.has_value()) {
                return _first.at(i.value());
            }
            return {};
        }

        [[nodiscard]] constexpr std::optional<uint32_t> indexOf(const VT1 &v) const noexcept {
            if(auto iter = find(v); iter != std::end(_first)) {
                return std::distance(std::begin(_first), iter);
            }
            return {};
        }

        [[nodiscard]] constexpr std::optional<uint32_t> rindexOf(const VT2 &v) const noexcept {
            if(auto iter = rfind(v); iter != std::end(_second)) {
                return std::distance(std::begin(_second), iter);
            }
            return {};
        }

        template<size_t idx>
        [[nodiscard]] constexpr std::pair<VT1, VT2> get() const noexcept {
            return {std::get<idx>(_first), std::get<idx>(_second)};
        }
    };
    template<typename VT1, typename VT2, typename... Rest>
    LookupTable(const VT1 &v1, const VT2 &v2, const Rest &...args)
        -> LookupTable<VT1, VT2, 1 + sizeof...(Rest) / 2>;

} // namespace util
