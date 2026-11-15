/**************************************************************************/
/* integrated_device.hpp                                                  */
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
 * @file integrated_device.hpp
 * @brief Declares is_integrated and shared_pool_of, how a device relates to host memory.
 * @see modules/platform/device/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstdint>

#include <sycl/sycl.hpp>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/machine/device_description.hpp>

namespace SushiRuntime
{
    namespace Internal::Device
    {
        /**
         * @brief Tells whether a device draws its memory from system RAM.
         *
         * Answers what SYCL reports through the host-unified-memory query. A backend that does
         * not answer, or throws, counts as discrete.
         */
        [[nodiscard]] SUSHIRUNTIME_API bool is_integrated(const sycl::device& device) noexcept;

        /**
         * @brief Returns the id of the pool a device draws on.
         * @param description The device's inventory record.
         * @param own_pool The pool id of the device when it has memory of its own.
         * @param host_pool The pool id of the host.
         * @return @p host_pool for an integrated device, @p own_pool otherwise.
         */
        [[nodiscard]] SUSHIRUNTIME_API std::uint32_t shared_pool_of(const DeviceDescription& description,
                                                                    std::uint32_t own_pool,
                                                                    std::uint32_t host_pool) noexcept;
    } // namespace Internal::Device
} // namespace SushiRuntime
