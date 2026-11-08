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
/* Licensed under the Apache License, Version 2.0 (the "License"); you    */
/* may not use this file except in compliance with the License. You may   */
/* obtain a copy of the License at                                        */
/*                                                                        */
/*     https://www.apache.org/licenses/LICENSE-2.0                        */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
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

        /**
         * @brief Returns the flavour a device serves for a request: the one asked, in order
         *        whenever the device's plain queue runs in order.
         * @param asked The flavour the caller asks for.
         * @param plain_in_order True when the device's plain queue runs in order.
         */
        [[nodiscard]] constexpr QueueFlavour served_flavour(QueueFlavour asked,
                                                            bool plain_in_order) noexcept
        {
            return QueueFlavour{asked.in_order || plain_in_order, asked.profiling};
        }
    } // namespace Device
} // namespace SushiRuntime
