/**************************************************************************/
/* probe_devices.hpp                                                      */
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
 * @file probe_devices.hpp
 * @brief Declares ProbeDevices, the port through which DeviceProfileProbe finds a device by its key.
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <optional>

#include <sycl/sycl.hpp>

#include <SushiRuntime/handle/device_key.hpp>
#include <SushiRuntime/memory/memory_space.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /** @brief Holds what the probe needs of one device: its queue and its memory space. */
        struct ProbeDevice
        {
            sycl::queue* queue = nullptr;                /**< Queue the probe's copies and kernels run on; not null. */
            const Memory::MemorySpace* space = nullptr;  /**< Memory space of the device; not null. */
        };

        /**
         * @brief Finds a device by its DeviceKey.
         *
         * The runtime answers it over its device set, so the probe never lists devices itself.
         */
        class ProbeDevices
        {
            public:
                virtual ~ProbeDevices() = default;

                /** @brief Returns the device with key, or nothing when the runtime has none. */
                [[nodiscard]] virtual std::optional<ProbeDevice> find(const DeviceKey& key) const = 0;
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
