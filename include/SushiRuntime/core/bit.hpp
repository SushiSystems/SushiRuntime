/**************************************************************************/
/* bit.hpp                                                                */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/**************************************************************************/

#pragma once

// Bit helpers standing in for C++20 <bit> (std::popcount / std::countr_zero) on
// the C++17 baseline. Restore the std:: versions when the standard is raised —
// see docs/slop/cpp_standard_migration.md.

#include <cstdint>

#if defined(_MSC_VER) && !defined(__clang__)
    #include <intrin.h>
#endif

namespace SushiRuntime
{
    namespace Core
    {
        /**
         * @brief Counts the set bits in a 64-bit value (std::popcount stand-in).
         * @param x The value to inspect.
         * @return The number of 1 bits, 0..64.
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
         * @brief Counts trailing zero bits in a 64-bit value (std::countr_zero stand-in).
         * @param x The value to inspect.
         * @return The number of trailing 0 bits; 64 when x is 0.
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
