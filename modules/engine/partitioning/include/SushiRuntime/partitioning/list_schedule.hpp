/**************************************************************************/
/* list_schedule.hpp                                                      */
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
 * @file list_schedule.hpp
 * @brief Declares ScheduledPlan and list_schedule(), insertion-based HEFT over a PartitionModel.
 * @see modules/engine/partitioning/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/partitioning/partition_model.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /** @brief Holds a placement, the order its nodes run in and its modelled step. */
        struct ScheduledPlan
        {
            std::vector<std::size_t> placement; /**< Device index of each node. */
            std::vector<std::size_t> order;     /**< Nodes in the order they were placed and run. */
            double step_time = 0.0;             /**< PartitionModel::step_time() of placement in order. */
        };

        /** @brief The most passes refine_schedule() makes over the nodes. */
        constexpr std::size_t K_REFINE_PASSES = 8;

        /**
         * @brief Returns the nodes of model by descending upward rank, equal ranks in recording order.
         *
         * Ranks are means over devices of node and edge times, so a node ranks at least as high as
         * each successor and the order runs every edge forward.
         */
        [[nodiscard]] SUSHIRUNTIME_API std::vector<std::size_t> rank_order(const PartitionModel& model);

        /**
         * @brief Places the nodes of model in rank order, each where a StepSimulation finishes it earliest.
         *
         * A node may fill an idle gap of a device (insertion). Equal finishes go to the lower
         * device index; a fixed node stays on its device.
         * @return Nothing when a movable node fits in no pool.
         */
        [[nodiscard]] SUSHIRUNTIME_API std::optional<ScheduledPlan> place_by_rank(const PartitionModel& model);

        /**
         * @brief Moves nodes of plan onto a neighbour's device while the step falls and the plan fits.
         *
         * Moves are tried in recording order, the order of plan kept, for at most K_REFINE_PASSES
         * passes; each pass prices at most two moves per edge.
         */
        SUSHIRUNTIME_API void refine_schedule(const PartitionModel& model, ScheduledPlan& plan);

        /** @brief Returns place_by_rank() refined, or nothing when a movable node fits in no pool. */
        [[nodiscard]] SUSHIRUNTIME_API std::optional<ScheduledPlan> list_schedule(const PartitionModel& model);
    } // namespace Internal::Coherence
} // namespace SushiRuntime
