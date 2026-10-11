/**************************************************************************/
/* dispatch_width.hpp                                                     */
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
 * @file dispatch_width.hpp
 * @brief Defines WIDTH_UNKNOWN, the marker of a dispatch with no element count.
 * @author Mustafa Garip
 */

#pragma once

#include <cstdint>

namespace SushiRuntime
{
    namespace Internal::Observability
    {
        /** @brief Marks a dispatch whose element count the runtime does not carry. */
        inline constexpr std::uint32_t WIDTH_UNKNOWN = 0xFFFFFFFFu;
    } // namespace Observability
} // namespace SushiRuntime
