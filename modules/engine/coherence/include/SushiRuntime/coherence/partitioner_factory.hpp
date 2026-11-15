/**************************************************************************/
/* partitioner_factory.hpp                                                */
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
 * @file partitioner_factory.hpp
 * @brief Declares PartitionerFactories, which build the two partitioners a session runs over a cost model.
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <functional>
#include <memory>

#include <SushiRuntime/coherence/cost_model.hpp>
#include <SushiRuntime/coherence/partitioner.hpp>
#include <SushiRuntime/coherence/placement_journal.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /** @brief Builds a partitioner priced by a cost model. */
        using PartitionerFactory = std::function<std::unique_ptr<Partitioner>(std::shared_ptr<const CostModel>)>;

        /** @brief Holds how to build the scheduler and the floor-only partitioner over any profiles. */
        struct PartitionerFactories
        {
            PartitionerFactory scheduler; /**< Builds the partitioner a plan is made with. */
            PartitionerFactory floor;     /**< Builds the partitioner that answers with the floor. */

            /** @brief Returns the factory of planned_by; null when it was not given. */
            [[nodiscard]] const PartitionerFactory& of(PlannedBy planned_by) const noexcept
            {
                return planned_by == PlannedBy::Floor ? floor : scheduler;
            }
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
