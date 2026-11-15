/**************************************************************************/
/* rebalance_decision.hpp                                                 */
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
 * @file rebalance_decision.hpp
 * @brief Declares decide_rebalance(), what a horizon does with the plan in force.
 * @see modules/engine/partition_session/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstdint>

#include <SushiRuntime/report/partition_report.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /** @brief Holds the margin a measured time may pass its model by before a horizon acts: 10%. */
        constexpr double K_DEFAULT_REBALANCE_MARGIN = 0.10;

        /** @brief Names what a horizon does with the plan in force. */
        enum class RebalanceAction : std::uint8_t
        {
            Keep,         /**< The plan stays. */
            Replan,       /**< A device ran slower than its model; fold its profile and partition again. */
            TryFloor,     /**< The plan measured slower than the floor's model; run the floor for one horizon. */
            KeepFloor,    /**< The floor on trial measured faster than the plan it replaced; it stays. */
            LeaveFloor    /**< The floor on trial measured no faster; the plan it replaced returns. */
        };

        /** @brief Holds what the session remembers between horizons about the floor check. */
        struct RebalanceState
        {
            bool floor_on_trial = false;        /**< Whether the floor runs for one horizon in place of the plan. */
            double replaced_step_seconds = 0.0; /**< Measured step of the plan the floor replaced. */
            bool pair_tried = false;            /**< Whether this plan and its floor were measured against each other. */
        };

        /**
         * @brief Returns what a horizon does, from the report of the plan in force and what it measured.
         *
         * A floor on trial is judged first. Then a device whose measured time passes its modelled time by
         * more than margin replans. Then a plan that is not the floor and measured a step above the floor's
         * modelled step by more than margin tries the floor, once per pair. A report with no measured
         * step, or a refused one, keeps the plan. The result is a pure function of its arguments.
         */
        [[nodiscard]] RebalanceAction decide_rebalance(const PartitionReport& report, double margin,
                                                       const RebalanceState& state) noexcept;

        /** @brief Returns the name of action, one lower-case word, as the placement journal writes it. */
        [[nodiscard]] const char* to_text(RebalanceAction action) noexcept;
    } // namespace Internal::Coherence
} // namespace SushiRuntime
