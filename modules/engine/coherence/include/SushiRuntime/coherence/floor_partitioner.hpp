/**************************************************************************/
/* floor_partitioner.hpp                                                  */
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
 * @file floor_partitioner.hpp
 * @brief Declares FloorPartitioner, which answers every problem with its floor plan.
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <memory>

#include <SushiRuntime/coherence/cost_model.hpp>
#include <SushiRuntime/coherence/partitioner.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /**
         * @brief Places every node where the floor plan puts it, with no node split.
         *
         * The answer has floor_taken set. The session runs it when the plan in force measured
         * slower than the floor's model.
         * @see docs/design/GLOBAL_HANDLE/PARTITIONING.md
         */
        class FloorPartitioner final : public Partitioner
        {
            public:
                /** @brief Builds a partitioner priced by costs, which must not be null. */
                explicit FloorPartitioner(std::shared_ptr<const CostModel> costs);

                [[nodiscard]] Result<PartitionAssignment, PartitionRefusal> partition(
                    const PartitionProblem& problem) const override;

            private:
                /// @brief The frozen cost model.
                std::shared_ptr<const CostModel> costs_;
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
