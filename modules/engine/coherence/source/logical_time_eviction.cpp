/**************************************************************************/
/* logical_time_eviction.cpp                                              */
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
 * @file logical_time_eviction.cpp
 * @brief Implements LogicalTimeEviction.
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/coherence/logical_time_eviction.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        std::optional<std::size_t>
        LogicalTimeEviction::choose(const std::vector<EvictionCandidate>& candidates) const
        {
            if (candidates.empty())
                return std::nullopt;

            std::size_t best = 0;
            for (std::size_t index = 1; index < candidates.size(); ++index)
            {
                const EvictionCandidate& challenger = candidates[index];
                const EvictionCandidate& holder = candidates[best];
                const bool later = challenger.next_read > holder.next_read;
                const bool cheaper = challenger.next_read == holder.next_read &&
                                     holder.sole_copy && !challenger.sole_copy;
                if (later || cheaper)
                    best = index;
            }
            return best;
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
