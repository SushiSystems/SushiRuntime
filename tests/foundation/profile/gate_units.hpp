/**************************************************************************/
/* gate_units.hpp                                                         */
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
 * @file gate_units.hpp
 * @brief Declares the functions of the translation units that compile the front at different levels.
 * @see tests/foundation/profile/README.md
 * @author Mustafa Garip
 */

#pragma once

namespace ProfileGateTests
{
    /** @brief Records one stage zone, one fine zone and one value in a unit compiled at level 0. */
    void record_at_level_zero();

    /** @brief Records one stage zone, one fine zone and one value in a unit compiled at level 1. */
    void record_at_level_one();

    /** @brief Records one stage zone, one fine zone and one detail zone in a unit compiled at level 2. */
    void record_at_level_two();

    /** @brief Records one zone of each level, 1 to 3, in a unit compiled at level 3. */
    void record_at_level_three();
} // namespace ProfileGateTests
