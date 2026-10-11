/**************************************************************************/
/* queue_flavour.hpp                                                      */
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
 * @file queue_flavour.hpp
 * @brief Declares QueueFlavour, the pair of SYCL queue properties a caller asks for.
 * @author Mustafa Garip
 * @see modules/platform/device/README.md
 */

#pragma once

namespace SushiRuntime
{
    namespace Internal::Device
    {
        /**
         * @brief Names the properties of one queue of a device.
         */
        struct QueueFlavour
        {
            bool in_order = false;  /**< The queue runs its submissions in submission order. */
            bool profiling = false; /**< The queue records event timestamps. */
        };

        /**
         * @brief Returns whether two flavours ask for the same properties.
         */
        [[nodiscard]] constexpr bool operator==(QueueFlavour left, QueueFlavour right) noexcept
        {
            return left.in_order == right.in_order && left.profiling == right.profiling;
        }

        /**
         * @brief Returns whether two flavours ask for different properties.
         */
        [[nodiscard]] constexpr bool operator!=(QueueFlavour left, QueueFlavour right) noexcept
        {
            return !(left == right);
        }
    } // namespace Device
} // namespace SushiRuntime
