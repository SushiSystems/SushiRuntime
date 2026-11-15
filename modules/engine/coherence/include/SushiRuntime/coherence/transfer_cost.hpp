/**************************************************************************/
/* transfer_cost.hpp                                                      */
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
 * @file transfer_cost.hpp
 * @brief Declares TransferCost, the function that prices one copy over one path.
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <functional>

#include <SushiRuntime/coherence/transfer_path.hpp>
#include <SushiRuntime/handle/memory_space_id.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /**
         * @brief Returns the cost of copying bytes from source to destination over path.
         *
         * The function must be pure: equal arguments give equal costs in every run. With a
         * device profile loaded, select_transfer_cost() supplies it; uniform_transfer_cost() stands in
         * when none is.
         */
        using TransferCost = std::function<double(const TransferPath& path, MemorySpaceID source,
                                                  MemorySpaceID destination, std::size_t bytes)>;

        /** @brief Returns a cost that prices every path alike, by its bytes. */
        [[nodiscard]] inline TransferCost uniform_transfer_cost()
        {
            return [](const TransferPath&, MemorySpaceID, MemorySpaceID, std::size_t bytes)
            {
                return static_cast<double>(bytes);
            };
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
