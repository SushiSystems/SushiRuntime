/****************************************************************/
/* dispatch_width.hpp                                           */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

/**
 * @file dispatch_width.hpp
 * @brief Defines WIDTH_UNKNOWN, the marker of a dispatch with no element count.
 * @author Mustafa Garip
 */

#pragma once

#include <cstdint>

namespace SushiRuntime
{
    namespace Core
    {
        /** @brief Marks a dispatch whose element count the runtime does not carry. */
        inline constexpr std::uint32_t WIDTH_UNKNOWN = 0xFFFFFFFFu;
    } // namespace Core
} // namespace SushiRuntime
