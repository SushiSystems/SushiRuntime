/**************************************************************************/
/* sycl_device_enumerator.hpp                                             */
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
 * @file sycl_device_enumerator.hpp
 * @brief Declares SYCLDeviceEnumerator, the DeviceEnumerator over every SYCL platform.
 * @author Mustafa Garip
 * @see modules/platform/topology/README.md
 */

#pragma once

#include <vector>

#include <sycl/sycl.hpp>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/topology/device_enumerator.hpp>

namespace SushiRuntime
{
    namespace Internal::Topology
    {
        /**
         * @brief Returns every SYCL device, in the order SYCLDeviceEnumerator gives its ids.
         *
         * Entry i is the device whose DeviceDescriptor has id i.
         * @throws sycl::exception When the device runtime cannot list the devices.
         */
        [[nodiscard]] SUSHIRUNTIME_API std::vector<sycl::device> sycl_devices();

        /**
         * @brief Lists the devices of every SYCL platform.
         *
         * A GPU's or an accelerator's PCI address text comes from the Intel device-info
         * extension where the device has it, from its vendor string otherwise.
         * @see modules/platform/topology/README.md
         */
        class SUSHIRUNTIME_API SYCLDeviceEnumerator final : public DeviceEnumerator
        {
            public:
                [[nodiscard]] Result<std::vector<DeviceDescriptor>> enumerate() override;
        };
    } // namespace Internal::Topology
} // namespace SushiRuntime
