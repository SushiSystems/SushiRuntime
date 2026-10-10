/**************************************************************************/
/* device_sized.hpp                                                       */
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
 * @file device_sized.hpp
 * @brief Declares DeviceSized, a launch whose live width is read on the device.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace SushiRuntime
{
    namespace API
    {
        /**
         * @brief Describes a launch that runs at capacity and reads its live width on the device.
         * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
         */
        struct DeviceSized
        {
            std::size_t capacity = 0; /**< Work-items launched every step. */

            /** @brief Work-items at or past this word return at once; null lets all of them run. */
            const std::uint32_t* live_width = nullptr;

            /** @brief One or two words handed to the kernel after its index, by its arity. */
            const std::uint32_t* live_words = nullptr;
        };

    } // namespace API
} // namespace SushiRuntime
