/**************************************************************************/
/* split_expansion.hpp                                                    */
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
 * @file split_expansion.hpp
 * @brief Declares SplitExpansion, expand_splits() and planned_splits(), split nodes as parts.
 * @see modules/engine/partitioning/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <vector>

#include <SushiRuntime/handle/device_key.hpp>
#include <SushiRuntime/partitioning/partition_assignment.hpp>
#include <SushiRuntime/partitioning/partition_problem.hpp>
#include <SushiRuntime/partitioning/split_sizing.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /** @brief Holds a problem in which each split node stands as one node per part. */
        struct SplitExpansion
        {
            PartitionProblem problem;         /**< The problem with every split node replaced by its parts. */
            std::vector<std::size_t> origin;  /**< Node of the original problem each node of problem stands for. */
        };

        /**
         * @brief Returns problem with each node that has parts replaced by one node per part.
         *
         * A part node is placed on its device, costs and writes its share, and reads whole; edges
         * follow the parts, and a later reader of a buffer the parts wrote reads their slices.
         * @param parts   Parts of each node of problem; fewer than two leaves the node whole.
         * @param devices Key of each device index the parts name.
         */
        [[nodiscard]] SplitExpansion expand_splits(const PartitionProblem& problem,
                                                   const std::vector<std::vector<SizedPart>>& parts,
                                                   const std::vector<DeviceKey>& devices);

        /**
         * @brief Returns the PlannedSplit of each node that has two parts or more, in recording order.
         *
         * A part reads its node's buffers whole and writes the bytes its range covers, so the
         * written ranges of one node are disjoint and cover the buffer.
         * @param parts   Parts of each node of problem, as expand_splits() took them.
         * @param devices Key of each device index the parts name.
         */
        [[nodiscard]] std::vector<PlannedSplit> planned_splits(const PartitionProblem& problem,
                                                               const std::vector<std::vector<SizedPart>>& parts,
                                                               const std::vector<DeviceKey>& devices);
    } // namespace Internal::Coherence
} // namespace SushiRuntime
