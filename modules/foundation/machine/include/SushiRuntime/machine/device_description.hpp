/**************************************************************************/
/* device_description.hpp                                                 */
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
 * @file device_description.hpp
 * @brief Declares DeviceDescription, the inventory record of one device.
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <sycl/sycl.hpp>

#include <SushiRuntime/machine/device_kind.hpp>

namespace SushiRuntime
{
    /**
     * @brief Describes one device of the inventory.
     */
    struct DeviceDescription
    {
        std::size_t index = 0;                         /**< Position in the device set. */
        std::string name;                              /**< Name the device reports. */
        DeviceKind kind = DeviceKind::Other;           /**< Class of the device. */
        sycl::backend backend{};                       /**< SYCL backend that drives it. */
        int numa_node = 0;                             /**< NUMA node the device sits on. */
        std::uint64_t memory_bytes = 0;                /**< Global memory of the device. */
        std::uint32_t compute_units = 0;               /**< Parallel compute units it has. */
    };
} // namespace SushiRuntime
