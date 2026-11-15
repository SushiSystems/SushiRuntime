/**************************************************************************/
/* runtime_memory_spaces.hpp                                              */
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
 * @file runtime_memory_spaces.hpp
 * @brief Declares RuntimeMemorySpaces, the memory spaces of a runtime's host and devices.
 * @see modules/engine/execution/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <memory>
#include <vector>

#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/memory/memory_probe.hpp>
#include <SushiRuntime/memory/memory_space.hpp>

namespace SushiRuntime
{
    namespace Internal::Execution
    {
        /** @brief Owns the memory spaces of one runtime: the host first, then each device in index order. */
        struct RuntimeMemorySpaces
        {
            std::vector<std::unique_ptr<Memory::MemorySpace>> owned; /**< The spaces. */
            std::vector<const Memory::MemorySpace*> views;           /**< The same spaces, read-only. */
        };

        /**
         * @brief Builds the host space through host_probe and one space per device of devices.
         *
         * Space ids follow engine/task's handle_location.hpp. A device copies without staging to
         * every other device of its SYCL context; each device draws on a pool of its own.
         */
        [[nodiscard]] RuntimeMemorySpaces build_memory_spaces(const Device::DeviceQueueSet& devices,
                                                              const Memory::MemoryProbe& host_probe);
    } // namespace Internal::Execution
} // namespace SushiRuntime
