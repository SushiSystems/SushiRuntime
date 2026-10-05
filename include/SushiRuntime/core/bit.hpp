/****************************************************************/
/* bit.hpp                                                      */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

/**
 * @file bit.hpp
 * @brief Defines popcount_u64 and countr_zero_u64 for the C++17 baseline.
 * @author Mustafa Garip
 * @see include/SushiRuntime/core/README.md
 */

#pragma once

#include <cstdint>

#if defined(_MSC_VER) && !defined(__clang__)
    #include <intrin.h>
#endif

namespace SushiRuntime
{
    namespace Core
    {
        /**
         * @brief Counts the set bits in a 64-bit unsigned integer.
         * 
         * Uses compiler intrinsics to efficiently calculate the population count.
         *
         * @param x The 64-bit unsigned integer to inspect.
         * @return The total number of bits set to 1, ranging from 0 to 64.
         */
        inline int popcount_u64(std::uint64_t x) noexcept
        {
            #if defined(_MSC_VER) && !defined(__clang__)
                return static_cast<int>(__popcnt64(x));
            #else
                return __builtin_popcountll(x);
            #endif
        }

        /**
         * @brief Counts the trailing zero bits in a 64-bit unsigned integer.
         * 
         * Uses compiler intrinsics to efficiently locate the least significant set bit.
         *
         * @param x The 64-bit unsigned integer to inspect.
         * @return The number of trailing 0 bits, or 64 if the value is zero.
         */
        inline int countr_zero_u64(std::uint64_t x) noexcept
        {
            if (x == 0)
                return 64;
            #if defined(_MSC_VER) && !defined(__clang__)
                unsigned long index;
                _BitScanForward64(&index, x);
                return static_cast<int>(index);
            #else
                return __builtin_ctzll(x);
            #endif
        }

    } // namespace Core
} // namespace SushiRuntime
