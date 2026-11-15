/**************************************************************************/
/* in_memory_placement_memory.hpp                                         */
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
 * @file in_memory_placement_memory.hpp
 * @brief Declares InMemoryPlacementMemory, a PlacementMemory that answers from handles a caller records.
 * @author Mustafa Garip
 * @see modules/engine/task/README.md
 */

#pragma once

#include <cstddef>
#include <optional>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include <SushiRuntime/handle/global_handle.hpp>

#include <SushiRuntime/task/placement_memory.hpp>

namespace SushiRuntime
{
    namespace Internal::Graph
    {
        /**
         * @brief Holds the context of each device and the device of each recorded handle.
         *
         * A handle has a device only once record() names it, so recorded_device() and owning_device()
         * answer alike. Device i lies in the SYCL context numbered context_of_device[i].
         * @see modules/engine/task/README.md
         */
        class InMemoryPlacementMemory final : public PlacementMemory
        {
            public:
                /**
                 * @brief Spans one device per entry of @p context_of_device.
                 * @param context_of_device The context number of each device; equal numbers share a context.
                 */
                explicit InMemoryPlacementMemory(std::vector<std::size_t> context_of_device)
                    : context_of_device_(std::move(context_of_device))
                {
                }

                /**
                 * @brief Records a handle's buffer as belonging to a device.
                 * @param handle The handle of the buffer.
                 * @param device  A device index below device_count().
                 */
                void record(GlobalHandle handle, std::size_t device)
                {
                    owners_[handle] = device;
                }

                /**
                 * @brief Records a handle's home as device-only memory, which no sibling device
                 *        reads; a recorded handle is host visible until this is called.
                 */
                void record_device_only(GlobalHandle handle)
                {
                    device_only_.insert(handle);
                }

                [[nodiscard]] bool host_visible(GlobalHandle handle) const override
                {
                    return owners_.count(handle) != 0 && device_only_.count(handle) == 0;
                }

                [[nodiscard]] std::optional<std::size_t> recorded_device(GlobalHandle handle) const override
                {
                    const auto found = owners_.find(handle);
                    if (found == owners_.end())
                        return std::nullopt;
                    return found->second;
                }

                [[nodiscard]] std::optional<std::size_t> owning_device(GlobalHandle handle) const override
                {
                    return recorded_device(handle);
                }

                [[nodiscard]] std::size_t device_count() const override
                {
                    return context_of_device_.size();
                }

                [[nodiscard]] bool shares_context(std::size_t first, std::size_t second) const override
                {
                    return context_of_device_.at(first) == context_of_device_.at(second);
                }

            private:
                std::vector<std::size_t> context_of_device_;
                std::map<GlobalHandle, std::size_t> owners_;
                std::set<GlobalHandle> device_only_;
        };
    } // namespace Internal::Graph
} // namespace SushiRuntime
