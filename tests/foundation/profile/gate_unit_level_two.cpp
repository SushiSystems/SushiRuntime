/**************************************************************************/
/* gate_unit_level_two.cpp                                                */
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
 * @file gate_unit_level_two.cpp
 * @brief Compiles the front at level 2, where stage and fine zones are written but not detail zones.
 * @see tests/foundation/profile/README.md
 * @author Mustafa Garip
 */

// The build may define the level for every unit; this one states its own.
#ifdef SR_PROFILE_LEVEL
#undef SR_PROFILE_LEVEL
#endif
#define SR_PROFILE_LEVEL 2

#include <SushiRuntime/profile/profile.hpp>

#include "gate_units.hpp"

namespace ProfileGateTests
{
    void record_at_level_two()
    {
        SR_ZONE(::SushiRuntime::Internal::Profile::process_collector(), "gate.two.stage");
        SR_ZONE_FINE(::SushiRuntime::Internal::Profile::process_collector(), "gate.two.fine");
        SR_ZONE_DETAIL(::SushiRuntime::Internal::Profile::process_collector(), "gate.two.detail");
    }
} // namespace ProfileGateTests
