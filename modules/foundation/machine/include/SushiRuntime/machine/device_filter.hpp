/**************************************************************************/
/* device_filter.hpp                                                      */
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
 * @file device_filter.hpp
 * @brief Declares DeviceFilter, the device classes and features a runtime accepts.
 * @author Mustafa Garip
 * @see modules/foundation/machine/README.md
 */

#pragma once

#include <SushiRuntime/machine/device_kind.hpp>

namespace SushiRuntime
{
    /**
     * @brief Names the device classes and features a runtime accepts.
     */
    struct DeviceFilter
    {
        bool include_cpu = true;         /**< Accepts CPU devices. */
        bool include_gpu = true;         /**< Accepts GPU devices. */
        bool include_accelerator = true; /**< Accepts accelerators such as FPGAs. */

        /**
         * @brief Drops devices that cannot execute double-precision arithmetic; off by default.
         * @see modules/foundation/machine/README.md
         */
        bool require_fp64 = false;
    };

    /**
     * @brief Returns whether a filter accepts the class of a device.
     * @param filter The filter a runtime is built with.
     * @param kind The class SYCL reports for the device; a device of no listed class is accepted.
     */
    [[nodiscard]] constexpr bool admits_class(const DeviceFilter& filter, DeviceKind kind) noexcept
    {
        switch (kind)
        {
            case DeviceKind::CPU:         return filter.include_cpu;
            case DeviceKind::GPU:         return filter.include_gpu;
            case DeviceKind::Accelerator: return filter.include_accelerator;
            case DeviceKind::Other:       return true;
        }
        return true;
    }

    /**
     * @brief Returns whether a filter accepts a device: its class, and fp64 when the filter requires it.
     * @param filter The filter a runtime is built with.
     * @param kind The class SYCL reports for the device.
     * @param has_fp64 Whether the device reports sycl::aspect::fp64.
     */
    [[nodiscard]] constexpr bool admits(const DeviceFilter& filter, DeviceKind kind, bool has_fp64) noexcept
    {
        return admits_class(filter, kind) && (has_fp64 || !filter.require_fp64);
    }
} // namespace SushiRuntime
