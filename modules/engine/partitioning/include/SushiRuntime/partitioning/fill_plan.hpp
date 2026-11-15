/**************************************************************************/
/* fill_plan.hpp                                                          */
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
 * @file fill_plan.hpp
 * @brief Declares fill_plan() and floor_plan(), the plans the partitioner holds its own plan against.
 * @see modules/engine/partitioning/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include <SushiRuntime/partitioning/partition_model.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /** @brief Holds the placement a fill walk made and whether every pool holds it. */
        struct FillWalk
        {
            std::vector<std::size_t> placement; /**< Device index of each node. */
            bool fits = false;                  /**< Whether every pool holds the placement. */
        };

        /** @brief Holds the floor: the fitting fill plan with the least modelled step time. */
        struct FloorPlan
        {
            std::vector<std::size_t> placement; /**< Device index of each node. */
            std::vector<std::size_t> devices;   /**< The subset the fill walked, in DeviceKey order. */
            double step_time = 0.0;             /**< Modelled seconds of one step. */
        };

        /**
         * @brief Returns the fill plan of model over the devices in subset.
         *
         * Devices go fastest alone first. Nodes go in recording order onto the current device, which
         * advances, never back, when its pool is full. The last device takes what is left, and fits
         * is then false. A fixed node stays on its device.
         * @param subset Device indices of model; must not be empty.
         */
        [[nodiscard]] FillWalk fill_plan(const PartitionModel& model, const std::vector<std::size_t>& subset);

        /**
         * @brief Returns the floor of model: the fitting fill plan of least step time over every device subset.
         *
         * Subsets are tried in increasing bit-mask order and a later one replaces the best only when
         * its step time is lower, so the floor is a pure function of the model.
         * @return Nothing when no subset's fill plan fits.
         */
        [[nodiscard]] std::optional<FloorPlan> floor_plan(const PartitionModel& model);
    } // namespace Internal::Coherence
} // namespace SushiRuntime
