/**************************************************************************/
/* eviction_policy.hpp                                                    */
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
 * @file eviction_policy.hpp
 * @brief Declares EvictionPolicy, the strategy that chooses which instance a full space gives up.
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <limits>
#include <optional>
#include <vector>

#include <SushiRuntime/handle/global_handle.hpp>
#include <SushiRuntime/handle/memory_space_id.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /** @brief Holds one instance a policy may choose to evict. */
        struct EvictionCandidate
        {
            /** @brief Value of next_read for an instance no node reads again. */
            static constexpr std::size_t K_NEVER = std::numeric_limits<std::size_t>::max();

            GlobalHandle handle;           /**< Buffer the instance belongs to. */
            MemorySpaceID space;           /**< Space the instance sits on. */
            std::size_t bytes = 0;         /**< Bytes the instance holds. */
            std::size_t next_read = K_NEVER; /**< Position in the recorded graph of its next use. */
            bool sole_copy = false;        /**< True when it holds bytes no other space holds. */
            double write_back_cost = 0.0;  /**< Price of copying its sole bytes home by the planner's TransferCost; 0 when sole_copy is false. */
        };

        /**
         * @brief Chooses the instance a space gives up when it cannot hold a new one.
         *
         * A policy reads only the candidates it is given, which the planner lists in handle order,
         * then space order, and never a clock or an address, so a plan stays a pure function of the
         * graph. A new policy adds a file and edits no sibling.
         */
        class EvictionPolicy
        {
            public:
                virtual ~EvictionPolicy() = default;

                /**
                 * @brief Returns the index of the candidate to evict.
                 * @return Nothing when candidates is empty.
                 */
                [[nodiscard]] virtual std::optional<std::size_t>
                choose(const std::vector<EvictionCandidate>& candidates) const = 0;
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
