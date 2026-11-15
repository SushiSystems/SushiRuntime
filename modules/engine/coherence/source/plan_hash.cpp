/**************************************************************************/
/* plan_hash.cpp                                                          */
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
 * @file plan_hash.cpp
 * @brief Implements plan_hash.
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/coherence/plan_hash.hpp>

#include <SushiRuntime/coherence/fnv_hash.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        std::uint64_t plan_hash(const CopyPlan& plan) noexcept
        {
            FNVHash hash;
            hash.fold(plan.steady ? 1U : 0U);
            hash.fold(plan.steps.size());
            for (const PlannedStep& step : plan.steps)
            {
                hash.fold(static_cast<std::uint64_t>(step.kind));
                hash.fold(step.handle.to_high_word());
                hash.fold(step.handle.to_low_word());
                hash.fold(step.space.value());
                hash.fold(step.source.value());
                hash.fold(step.begin);
                hash.fold(step.end);
                hash.fold(step.alignment);
                hash.fold(step.path);
                hash.fold(step.node);
                hash.fold(step.evicts ? 1U : 0U);
            }
            return hash.value();
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
