/**************************************************************************/
/* handle_location.hpp                                                    */
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
 * @file handle_location.hpp
 * @brief Declares the memory space of each device and the lookup of a handle's device instance.
 * @author Mustafa Garip
 * @see modules/engine/task/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include <SushiRuntime/handle/global_handle.hpp>
#include <SushiRuntime/handle/handle_resolver.hpp>
#include <SushiRuntime/handle/memory_space_id.hpp>

namespace SushiRuntime
{
    namespace Internal::Graph
    {
        /**
         * @brief Returns the memory space of host memory no device owns.
         *
         * Space 0 is the host; device i is space i + 1. This header is the one place that rule
         * is written.
         */
        [[nodiscard]] constexpr MemorySpaceID host_memory_space() noexcept
        {
            return MemorySpaceID(0);
        }

        /** @brief Returns the memory space of device @p device: its index plus one. */
        [[nodiscard]] constexpr MemorySpaceID device_memory_space(std::size_t device) noexcept
        {
            return MemorySpaceID(static_cast<std::uint32_t>(device + 1));
        }

        /** @brief Holds the device a handle has an instance on and that instance's address. */
        struct HandleLocation
        {
            std::size_t device = 0;  /**< Lowest device index that holds an instance. */
            void* address = nullptr; /**< Address of the instance on that device. */
        };

        /**
         * @brief Returns the instance of @p handle on the lowest device that holds one.
         * @param device_count Devices to look on, in index order.
         * @return Nothing for the null handle, a retired handle, or one with no device instance.
         */
        [[nodiscard]] inline std::optional<HandleLocation>
        locate(const HandleResolver& resolver, GlobalHandle handle, std::size_t device_count) noexcept
        {
            if (handle.is_null())
                return std::nullopt;
            for (std::size_t device = 0; device < device_count; ++device)
            {
                const ResolveOutcome outcome = resolver.resolve(handle, device_memory_space(device));
                if (outcome.status == HandleStatus::Ok)
                    return HandleLocation{device, outcome.address};
                if (outcome.status != HandleStatus::NotResident)
                    return std::nullopt;
            }
            return std::nullopt;
        }

        /**
         * @brief Returns the address of @p handle's instance on @p device, the pointer a node
         *        dispatched there works on.
         * @return Null when the handle has no live instance on that device.
         */
        [[nodiscard]] inline void* resolve_on_device(const HandleResolver& resolver,
                                                     GlobalHandle handle,
                                                     std::size_t device) noexcept
        {
            const ResolveOutcome outcome = resolver.resolve(handle, device_memory_space(device));
            return outcome.status == HandleStatus::Ok ? outcome.address : nullptr;
        }
    } // namespace Internal::Graph
} // namespace SushiRuntime
