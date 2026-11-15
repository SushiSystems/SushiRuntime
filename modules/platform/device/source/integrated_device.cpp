/**************************************************************************/
/* integrated_device.cpp                                                  */
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
 * @file integrated_device.cpp
 * @brief Implements is_integrated and shared_pool_of.
 * @see modules/platform/device/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/device/integrated_device.hpp>

namespace SushiRuntime
{
    namespace Internal::Device
    {
        bool is_integrated(const sycl::device& device) noexcept
        {
            try
            {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
                return device.get_info<sycl::info::device::host_unified_memory>();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
            }
            catch (...)
            {
                return false;
            }
        }

        std::uint32_t shared_pool_of(const DeviceDescription& description, std::uint32_t own_pool,
                                     std::uint32_t host_pool) noexcept
        {
            return description.integrated ? host_pool : own_pool;
        }
    } // namespace Internal::Device
} // namespace SushiRuntime
