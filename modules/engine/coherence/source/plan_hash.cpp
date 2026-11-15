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

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        namespace
        {
            constexpr std::uint64_t K_FNV_OFFSET = 0xcbf29ce484222325ULL;
            constexpr std::uint64_t K_FNV_PRIME = 0x100000001b3ULL;

            /** @brief Folds the eight little-endian bytes of value into hash. */
            void fold(std::uint64_t& hash, std::uint64_t value) noexcept
            {
                for (unsigned shift = 0; shift < 64; shift += 8)
                {
                    hash ^= (value >> shift) & 0xffU;
                    hash *= K_FNV_PRIME;
                }
            }
        } // namespace

        std::uint64_t plan_hash(const CopyPlan& plan) noexcept
        {
            std::uint64_t hash = K_FNV_OFFSET;
            fold(hash, plan.steady ? 1U : 0U);
            fold(hash, plan.steps.size());
            for (const PlannedStep& step : plan.steps)
            {
                fold(hash, static_cast<std::uint64_t>(step.kind));
                fold(hash, step.handle.to_high_word());
                fold(hash, step.handle.to_low_word());
                fold(hash, step.space.value());
                fold(hash, step.source.value());
                fold(hash, step.begin);
                fold(hash, step.end);
                fold(hash, step.alignment);
                fold(hash, step.path);
                fold(hash, step.node);
                fold(hash, step.evicts ? 1U : 0U);
            }
            return hash;
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
