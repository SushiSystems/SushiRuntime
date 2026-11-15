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

#include <cstdint>
#include <memory>
#include <set>
#include <vector>

#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/memory/file_memory_space.hpp>
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
            std::set<std::uint32_t> vacant_file_ids;                 /**< Ids of removed file spaces, lowest first. */
        };

        /**
         * @brief Builds the host space through host_probe and one space per device of devices.
         *
         * Space ids follow engine/task's handle_location.hpp. A device copies without staging to
         * every other device of its SYCL context. A discrete device draws on a pool of its own.
         * An integrated device names the host's pool and offers at most the host's usable bytes.
         */
        [[nodiscard]] RuntimeMemorySpaces build_memory_spaces(const Device::DeviceQueueSet& devices,
                                                              const Memory::MemoryProbe& host_probe);

        /**
         * @brief Adds the space of a file range under the lowest vacant file id, else under the
         *        count of spaces, so a space's id stays its index.
         * @return The new space; spaces owns it.
         */
        [[nodiscard]] const Memory::FileMemorySpace& add_file_space_to(RuntimeMemorySpaces& spaces, Memory::FileRange range);

        /**
         * @brief Replaces the file space with id by an empty one and marks the id vacant.
         * @return Whether id named a file space that was not vacant; any other id is left alone.
         */
        bool remove_file_space_from(RuntimeMemorySpaces& spaces, MemorySpaceID id);
    } // namespace Internal::Execution
} // namespace SushiRuntime
