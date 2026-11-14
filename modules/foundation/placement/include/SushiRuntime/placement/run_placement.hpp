/**************************************************************************/
/* run_placement.hpp                                                      */
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
 * @file run_placement.hpp
 * @brief Declares RunPlacement, the answer a run placement policy gives.
 * @author Mustafa Garip
 * @see docs/design/LAYERED_EXECUTION/RUN_PLACEMENT.md
 */

#pragma once

#include <cstdint>

namespace SushiRuntime
{
    /**
     * @brief Names where the steps of one run go.
     */
    enum class RunPlacement : std::uint8_t
    {
        Device, /**< The runtime's own path: the layer its step loop picks for the plan. */
        Host    /**< The calling thread, through each node's host form, when the plan allows it. */
    };
} // namespace SushiRuntime
