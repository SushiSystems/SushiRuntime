/**************************************************************************/
/* run_description.hpp                                                    */
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
 * @file run_description.hpp
 * @brief Declares RunDescription, what a run placement policy reads of one run.
 * @author Mustafa Garip
 * @see docs/design/LAYERED_EXECUTION/RUN_PLACEMENT.md
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace SushiRuntime
{
    /**
     * @brief Holds what the runtime measures of one run before its first step.
     *
     * Every field is a count the runtime knows without knowing what a node computes.
     * @see docs/design/LAYERED_EXECUTION/RUN_PLACEMENT.md
     */
    struct RunDescription
    {
        std::size_t node_count = 0;       /**< Nodes one step runs. */
        std::size_t steps = 0;            /**< Steps the run asks for; 1 for run_for() and run_until(). */
        std::uint64_t launched_items = 0; /**< Sum of the known launched widths, as they stand before the first step. */
        std::size_t unsized_nodes = 0;    /**< Nodes whose launched width is unknown. */
        std::uint64_t declared_flops = 0; /**< Sum of the NodeCost::flops the recorder stated. */
        std::uint64_t declared_bytes = 0; /**< Sum of the NodeCost::bytes the recorder stated. */
        std::size_t costed_nodes = 0;     /**< Nodes that carry a NodeCost. */
    };
} // namespace SushiRuntime
