/**************************************************************************/
/* gate_unit_level_one.cpp                                                */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the PolyForm Noncommercial License 1.0.0 (the           */
/* "License"); you may not use this file except in compliance with the    */
/* License. You may obtain a copy of the License at                       */
/*                                                                        */
/*     https://polyformproject.org/licenses/noncommercial/1.0.0           */
/*                                                                        */
/* Noncommercial use is free. Commercial use requires a separate licence  */
/* from Sushi Systems; see COMMERCIAL.md. The software is provided        */
/* "as is", without warranty of any kind.                                 */
/**************************************************************************/

/**
 * @file gate_unit_level_one.cpp
 * @brief Compiles the front at level 1, where only stage zones and values are written.
 * @see tests/foundation/profile/README.md
 * @author Mustafa Garip
 */

// The build may define the level for every unit; this one states its own.
#ifdef SR_PROFILE_LEVEL
#undef SR_PROFILE_LEVEL
#endif
#define SR_PROFILE_LEVEL 1

#include <SushiRuntime/profile/profile.hpp>

#include "gate_units.hpp"

namespace ProfileGateTests
{
    void record_at_level_one()
    {
        SR_ZONE("gate.one.stage");
        SR_ZONE_FINE("gate.one.fine");
        SR_VALUE("gate.one.value", "", 1.0);
    }
} // namespace ProfileGateTests
