/**************************************************************************/
/* rebalance_decision.cpp                                                 */
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
 * @file rebalance_decision.cpp
 * @brief Implements decide_rebalance().
 * @see modules/engine/partition_session/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/partition_session/rebalance_decision.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        namespace
        {
            /** @brief Returns whether measured passes modelled by more than margin. */
            [[nodiscard]] bool beyond(double measured, double modelled, double margin) noexcept
            {
                return modelled > 0.0 && measured > modelled * (1.0 + margin);
            }

            /** @brief Returns whether a device that holds nodes measured slower than its model. */
            [[nodiscard]] bool device_slower(const PartitionReport& report, double margin) noexcept
            {
                for (const PartitionDeviceRow& row : report.rows)
                {
                    if (row.nodes != 0 && beyond(row.measured_seconds, row.modelled_seconds, margin))
                        return true;
                }
                return false;
            }
        } // namespace

        RebalanceAction decide_rebalance(const PartitionReport& report, double margin,
                                         const RebalanceState& state) noexcept
        {
            if (report.refused || report.measured_step_seconds <= 0.0)
                return RebalanceAction::Keep;
            if (state.floor_on_trial)
                return report.measured_step_seconds < state.replaced_step_seconds ? RebalanceAction::KeepFloor
                                                                                  : RebalanceAction::LeaveFloor;
            if (device_slower(report, margin))
                return RebalanceAction::Replan;
            if (!report.floor_taken && !state.pair_tried &&
                beyond(report.measured_step_seconds, report.floor_step_seconds, margin))
                return RebalanceAction::TryFloor;
            return RebalanceAction::Keep;
        }

        const char* to_text(RebalanceAction action) noexcept
        {
            switch (action)
            {
                case RebalanceAction::Keep:
                    return "keep";
                case RebalanceAction::Replan:
                    return "replan";
                case RebalanceAction::TryFloor:
                    return "try_floor";
                case RebalanceAction::KeepFloor:
                    return "keep_floor";
                case RebalanceAction::LeaveFloor:
                    return "leave_floor";
            }
            return "keep";
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
