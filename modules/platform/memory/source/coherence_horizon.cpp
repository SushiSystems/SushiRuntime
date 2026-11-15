/**************************************************************************/
/* coherence_horizon.cpp                                                  */
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
 * @file coherence_horizon.cpp
 * @brief Implements CoherenceHorizon.
 * @author Mustafa Garip
 */

#include <SushiRuntime/memory/coherence_horizon.hpp>

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        CoherenceHorizon::CoherenceHorizon(std::size_t steps_per_horizon) noexcept
            : steps_per_horizon_(steps_per_horizon == 0 ? 1 : steps_per_horizon)
        {

        }

        bool CoherenceHorizon::advance(CoherenceHistory& history, std::size_t steps)
        {
            pending_ += steps;
            if (pending_ < steps_per_horizon_)
                return false;
            pending_ = 0;
            static_cast<void>(history.retire_coherence_history());
            return true;
        }
    } // namespace Internal::Memory
} // namespace SushiRuntime
