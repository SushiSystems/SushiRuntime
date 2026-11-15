/**************************************************************************/
/* partition_apply.hpp                                                    */
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
 * @file partition_apply.hpp
 * @brief Declares apply_partition(), which puts a graph's nodes where an assignment says.
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <SushiRuntime/coherence/graph_partition_problem.hpp>
#include <SushiRuntime/coherence/partition_assignment.hpp>
#include <SushiRuntime/core/result.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /**
         * @brief States the assigned device on every node the partitioner was free to move.
         *
         * A placed node keeps its placement. The planner then allocates each planned buffer on its
         * first writer's device, its assigned home. A failed call changes nothing.
         * @return InvalidArgument when the assignment does not fit the problem.
         */
        [[nodiscard]] Result<void> apply_partition(const GraphPartitionProblem& graph,
                                                   const PartitionAssignment& assignment);
    } // namespace Internal::Coherence
} // namespace SushiRuntime
