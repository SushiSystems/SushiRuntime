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
 * @brief Declares InMemoryPlacementMemory, a PlacementMemory that answers from addresses a caller records.
 * @author Mustafa Garip
 * @see modules/engine/task/README.md
 */

#pragma once

#include <cstddef>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include <SushiRuntime/task/placement_memory.hpp>

namespace SushiRuntime
{
    namespace Internal::Graph
    {
        /**
         * @brief Holds the context of each device and the device of each recorded address.
         *
         * An address has a device only once record() names it, so recorded_device() and owning_device()
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
                 * @brief Records an address as belonging to a device.
                 * @param address The first address of the allocation.
                 * @param device  A device index below device_count().
                 */
                void record(const void* address, std::size_t device)
                {
                    owners_[address] = device;
                }

                [[nodiscard]] std::optional<std::size_t> recorded_device(const void* address) const override
                {
                    const auto found = owners_.find(address);
                    if (found == owners_.end())
                        return std::nullopt;
                    return found->second;
                }

                [[nodiscard]] std::optional<std::size_t> owning_device(const void* address) const override
                {
                    return recorded_device(address);
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
                std::unordered_map<const void*, std::size_t> owners_;
        };
    } // namespace Internal::Graph
} // namespace SushiRuntime
