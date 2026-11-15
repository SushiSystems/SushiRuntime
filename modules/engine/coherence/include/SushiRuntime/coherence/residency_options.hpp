/**************************************************************************/
/* residency_options.hpp                                                  */
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
 * @file residency_options.hpp
 * @brief Declares ResidencyOptions, what the planner needs to keep instances within capacity.
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <set>

#include <SushiRuntime/coherence/eviction_policy.hpp>
#include <SushiRuntime/coherence/pin_set.hpp>
#include <SushiRuntime/handle/global_handle.hpp>
#include <SushiRuntime/handle/memory_space_id.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /**
         * @brief Holds the capacity rules of one planner.
         *
         * With no policy the planner does not track capacity and frees every instance it made at
         * the graph end. With a policy it tracks the bytes it plans on each space, evicts through
         * the policy when a space is full, and keeps its instances resident across steps.
         * The policy and the pins must outlive the planner.
         */
        struct ResidencyOptions
        {
            const EvictionPolicy* policy = nullptr;  /**< Chooses victims; null turns the rules off. */
            const PinSet* pins = nullptr;            /**< Instances never evicted; may be null. */
            MemorySpaceID host;                      /**< Space an eviction writes back to. */
            std::set<GlobalHandle> host_read;        /**< Buffers whose home the host reads between steps. */
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
