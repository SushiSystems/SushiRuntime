/**************************************************************************/
/* list_scheduling_partitioner.hpp                                        */
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

/**
 * @file list_scheduling_partitioner.hpp
 * @brief Declares ListSchedulingPartitioner, the Partitioner that ranks, places, refines and checks the floor.
 * @see modules/engine/partitioning/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstdint>
#include <memory>

#include <SushiRuntime/device_profile/cost_model.hpp>
#include <SushiRuntime/partitioning/partitioner.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /**
         * @brief Shares a graph across devices by list scheduling in the manner of HEFT.
         *
         * Nodes go by descending upward rank to the device that finishes them earliest within
         * capacity, move to a neighbour's device while the step falls, and the floor replaces a
         * slower plan. A declared split is taken when the node fits no pool whole or the step falls.
         * @see modules/engine/partitioning/README.md
         */
        class ListSchedulingPartitioner final : public Partitioner
        {
            public:
                /** @brief Says whether the partitioner may cut a node that declares a split. */
                enum class Splitting : std::uint8_t
                {
                    Allowed, /**< A declared split is taken when the node fits no pool whole or the step falls. */
                    Off      /**< Every node runs whole. */
                };

                /** @brief Builds a partitioner priced by costs, which must not be null. */
                explicit ListSchedulingPartitioner(std::shared_ptr<const CostModel> costs,
                                                   Splitting splitting = Splitting::Allowed);

                [[nodiscard]] Result<PartitionAssignment, PartitionRefusal> partition(
                    const PartitionProblem& problem) const override;

            private:
                /// @brief The frozen cost model.
                std::shared_ptr<const CostModel> costs_;

                /// @brief Whether a declared split may be taken.
                Splitting splitting_;
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
