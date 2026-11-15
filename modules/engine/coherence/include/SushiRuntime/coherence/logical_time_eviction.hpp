/**************************************************************************/
/* logical_time_eviction.hpp                                              */
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
 * @file logical_time_eviction.hpp
 * @brief Declares LogicalTimeEviction, which evicts the instance read furthest ahead in the graph.
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <SushiRuntime/coherence/eviction_policy.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /**
         * @brief Evicts the instance whose next read is furthest in the recorded graph.
         *
         * Of equal next reads, an instance that needs no write-back goes first; of the rest, the
         * first candidate wins. The policy reads graph positions and never the clock.
         */
        class LogicalTimeEviction final : public EvictionPolicy
        {
            public:
                [[nodiscard]] std::optional<std::size_t>
                choose(const std::vector<EvictionCandidate>& candidates) const override;
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
