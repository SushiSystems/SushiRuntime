/**************************************************************************/
/* test_rebalance_decision.cpp                                            */
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
 * @file test_rebalance_decision.cpp
 * @brief Tests what decide_rebalance() makes of a measured report and the state of the floor check.
 * @see tests/engine/partition_session/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <string>

#include <SushiRuntime/partition_session/rebalance_decision.hpp>

#include "support/partition_problem_builder.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;

namespace
{
    /** @brief Holds one case: what was measured, the floor check's state and the action expected. */
    struct DecisionRow
    {
        const char* name;            /**< What the case shows. */
        double device_measured;      /**< Measured seconds of the one device that holds nodes; its model says 1. */
        double step_measured;        /**< Measured step; the plan's model says 1 and the floor's 1.5. */
        bool floor_taken;            /**< Whether the plan in force is the floor. */
        RebalanceState state;        /**< The floor check's state. */
        RebalanceAction expected;    /**< The action the case expects. */
    };

    /** @brief Returns the report of a plan modelled at one second a step, its floor at 1.5, as row measured it. */
    PartitionReport report_of(const DecisionRow& row)
    {
        PartitionReport report;
        PartitionDeviceRow device;
        device.device = host_key(1);
        device.nodes = 2;
        device.modelled_seconds = 1.0;
        device.measured_seconds = row.device_measured;
        report.rows.push_back(device);
        report.step_seconds = 1.0;
        report.measured_step_seconds = row.step_measured;
        report.floor_step_seconds = 1.5;
        report.floor_taken = row.floor_taken;
        return report;
    }

    class Small_RebalanceDecision : public ::testing::TestWithParam<DecisionRow>
    {

    };

    TEST_P(Small_RebalanceDecision, ActsOnTheMeasuredTimesAgainstTheirModelsWithTheTenPercentMargin)
    {
        const DecisionRow& row = GetParam();

        EXPECT_EQ(decide_rebalance(report_of(row), K_DEFAULT_REBALANCE_MARGIN, row.state), row.expected);
    }

    INSTANTIATE_TEST_SUITE_P(
        Rows, Small_RebalanceDecision,
        ::testing::Values(
            DecisionRow{"NothingMeasuredKeeps", 0.0, 0.0, false, RebalanceState{}, RebalanceAction::Keep},
            DecisionRow{"WithinTheMarginKeeps", 1.09, 1.09, false, RebalanceState{}, RebalanceAction::Keep},
            DecisionRow{"ASlowerDeviceReplans", 1.2, 1.2, false, RebalanceState{}, RebalanceAction::Replan},
            DecisionRow{"AStepAboveTheFloorTriesTheFloor", 1.0, 1.7, false, RebalanceState{},
                        RebalanceAction::TryFloor},
            DecisionRow{"AFloorInForceIsNotTriedAgain", 1.0, 1.7, true, RebalanceState{}, RebalanceAction::Keep},
            DecisionRow{"ATriedPairIsNotTriedAgain", 1.0, 1.7, false, RebalanceState{false, 0.0, true},
                        RebalanceAction::Keep},
            DecisionRow{"AFasterFloorOnTrialStays", 1.0, 1.5, true, RebalanceState{true, 1.7, false},
                        RebalanceAction::KeepFloor},
            DecisionRow{"ASlowerFloorOnTrialLeaves", 1.0, 1.8, true, RebalanceState{true, 1.7, false},
                        RebalanceAction::LeaveFloor}),
        [](const ::testing::TestParamInfo<DecisionRow>& info) { return std::string(info.param.name); });
} // namespace
