#pragma once
#include <cstdint>

namespace data {
    inline constexpr uint32_t rotl(uint32_t value, int32_t distance) {
        constexpr auto SHIFT_MASK = 0x1F;
        constexpr auto INT_BITS = 32;
        distance &= SHIFT_MASK;
        uint64_t shifted = static_cast<uint64_t>(value) << distance;
        return static_cast<uint32_t>((shifted >> INT_BITS) | shifted);
    }
    inline constexpr uint32_t rotr(uint32_t value, int32_t distance) {
        return rotl(value, -distance);
    }
    inline constexpr uint32_t byteSwap(uint32_t value) {
        constexpr auto SHIFT_MASK = 0x00FF00FF;
        constexpr auto SHIFT_BITS = 8;
        return (rotl(value, SHIFT_BITS) & ~SHIFT_MASK) | (rotr(value, SHIFT_BITS) & SHIFT_MASK);
    }
    inline constexpr uint32_t nibSwap(uint32_t value) {
        constexpr auto SHIFT_MASK = 0x0F0F0F0F;
        constexpr auto SHIFT_BITS = 4;
        return (rotl(value, SHIFT_BITS) & ~SHIFT_MASK) | (rotr(value, SHIFT_BITS) & SHIFT_MASK);
    }

    class IdObfuscator {
        constexpr static uint32_t OFFSET = 0x539137DA;
        constexpr static uint32_t XOR = 0x65294673;
        constexpr static uint32_t SHIFT = 7;

    public:
        constexpr static uint32_t INVALID_ID = 0xFFFFFFFF;
        constexpr static uint32_t INVALID_OBFUSCATED_ID = 0;
        IdObfuscator() = delete;

        constexpr static uint32_t obfuscate(uint32_t value) noexcept {
            // 0xFFFFFFFF maps to 0, and otherwise obfuscated ID
            // Function that obfuscates 32-bit integer into another 32-bit non-zero integer
            auto v1 = data::byteSwap(value - OFFSET);
            auto v2 = data::nibSwap(v1);
            auto v3 = data::rotl(v2, SHIFT);
            auto v4 = v3 ^ XOR;
            return v4;
        }

        constexpr static uint32_t deobfuscate(uint32_t value) noexcept {
            // Inverse
            auto v4 = value ^ XOR;
            auto v3 = data::rotr(v4, SHIFT);
            auto v2 = data::nibSwap(v3);
            auto v1 = data::byteSwap(v2);
            return v1 + OFFSET;
        }
    };
} // namespace data
