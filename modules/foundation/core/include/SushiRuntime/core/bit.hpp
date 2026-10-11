/**************************************************************************/
/* bit.hpp                                                                */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the PolyForm Noncommercial License 1.0.0 (the           */
/* "License"); you may not use this file except in compliance with the    */
/* License. You may obtain a copy of the License at                       */
/*                                                                        */
/*     https://polyformproject.org/licenses/noncommercial/1.0.0           */
/*                                                                        */
/* Noncommercial use is free. Commercial use requires a separate licence  */
/* from Sushi Systems; see COMMERCIAL.md. The software is provided        */
/* "as is", without warranty of any kind.                                 */
/**************************************************************************/

/**
 * @file bit.hpp
 * @brief Defines popcount_u64 and countr_zero_u64 for the C++17 baseline.
 * @author Mustafa Garip
 * @see modules/foundation/core/README.md
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
         * Uses __popcnt64 on MSVC and __builtin_popcountll on other compilers.
         *
         * @param x The 64-bit unsigned integer to inspect.
         * @return The total number of bits set to 1, ranging from 0 to 64.
         */
        [[nodiscard]] inline int popcount_u64(std::uint64_t x) noexcept
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
         * Uses _BitScanForward64 on MSVC and __builtin_ctzll on other compilers.
         *
         * @param x The 64-bit unsigned integer to inspect.
         * @return The number of trailing 0 bits, or 64 if the value is zero.
         */
        [[nodiscard]] inline int countr_zero_u64(std::uint64_t x) noexcept
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
