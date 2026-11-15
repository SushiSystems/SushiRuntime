/**************************************************************************/
/* device_memory_probe.hpp                                                */
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
 * @file device_memory_probe.hpp
 * @brief Declares DeviceMemoryProbe, the MemoryProbe that reads one device of a DeviceQueueSet.
 * @see modules/platform/device/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <limits>

#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/memory/memory_probe.hpp>

namespace SushiRuntime
{
    namespace Internal::Device
    {
        /**
         * @brief Reads the global and free memory of one device through its DeviceQueueSet.
         *
         * The total is the memory the device's description holds; the free bytes are what
         * DeviceQueueSet::free_memory() answers. The set must outlive the probe. A total limit
         * caps both: an integrated device cannot offer more than the host memory it shares.
         */
        class DeviceMemoryProbe final : public Memory::MemoryProbe
        {
            public:
                /**
                 * @brief Builds the probe of device index of devices.
                 * @param total_limit The most bytes the probe reports as total or free.
                 */
                DeviceMemoryProbe(const DeviceQueueSet& devices, std::size_t index,
                                  std::size_t total_limit = std::numeric_limits<std::size_t>::max()) noexcept;

                [[nodiscard]] Memory::ProbedMemory probe() const override;

            private:
                /// @brief The set the device belongs to.
                const DeviceQueueSet* devices_;

                /// @brief Index of the device in the set.
                std::size_t index_;

                /// @brief The most bytes reported as total or free.
                std::size_t total_limit_;
        };
    } // namespace Internal::Device
} // namespace SushiRuntime
