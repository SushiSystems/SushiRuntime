/**************************************************************************/
/* coherence_horizon.hpp                                                  */
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
 * @file coherence_horizon.hpp
 * @brief Declares CoherenceHorizon, which retires a directory's coherence history every N steps.
 * @see modules/platform/memory/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/memory/coherence_history.hpp>

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        /**
         * @brief Counts the steps a runtime ran and retires the coherence history it is
         *        given each time the count reaches the interval.
         *
         * A step driver calls advance() between runs, when no plan is being made.
         */
        class SUSHIRUNTIME_API CoherenceHorizon final
        {
            public:
                /** @brief Steps between two horizons when the runtime names no other interval. */
                static constexpr std::size_t K_STEPS_PER_HORIZON = 64;

                /**
                 * @brief Builds a horizon that falls every steps_per_horizon steps.
                 * @param steps_per_horizon Zero counts as one.
                 */
                explicit CoherenceHorizon(std::size_t steps_per_horizon = K_STEPS_PER_HORIZON) noexcept;

                /**
                 * @brief Adds steps to the count and retires history when
                 *        the count reaches the interval; the count then starts again from zero.
                 * @return Whether a horizon fell.
                 */
                bool advance(CoherenceHistory& history, std::size_t steps);

                /** @brief Returns the steps between two horizons. */
                [[nodiscard]] std::size_t steps_per_horizon() const noexcept
                {
                    return steps_per_horizon_;
                }

            private:
                /// @brief Steps between two horizons; at least one.
                std::size_t steps_per_horizon_;

                /// @brief Steps run since the last horizon.
                std::size_t pending_ = 0;
        };
    } // namespace Internal::Memory
} // namespace SushiRuntime
