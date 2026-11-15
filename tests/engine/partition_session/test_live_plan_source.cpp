/**************************************************************************/
/* test_live_plan_source.cpp                                              */
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
 * @file test_live_plan_source.cpp
 * @brief Tests that LivePlanSource plans with the scheduler over the shared profiles and judges each horizon.
 * @see tests/engine/partition_session/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include <SushiRuntime/partition_session/live_plan_source.hpp>
#include <SushiRuntime/partitioning/problem_hash.hpp>
#include <SushiRuntime/partitioning/partition_report_builder.hpp>

#include "support/partition_problem_builder.hpp"
#include "rebalancing_parts.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;

namespace
{
    TEST(Small_LivePlanSource, TheFirstPlanIsTheSchedulersAnswerPricedByTheProfilesInMemory)
    {
        const std::shared_ptr<ProfileStore> store = speed_store({1.0, 1.0});
        LivePlanSource source(store, runtime_factories(), K_DEFAULT_REBALANCE_MARGIN);

        const Result<PartitionPlan> plan = source.first(independent_nodes(2, 2));

        ASSERT_TRUE(plan.has_value());
        EXPECT_EQ(plan.value().problem_hash, problem_hash(independent_nodes(2, 2)));
        EXPECT_EQ(plan.value().planned_by, PlannedBy::Scheduler);
        EXPECT_EQ(plan.value().model, store->model());
        ASSERT_TRUE(plan.value().answer.has_value());
        EXPECT_NE(plan.value().answer.value().node_devices[0], plan.value().answer.value().node_devices[1]);
    }

    TEST(Small_LivePlanSource, AHorizonOfAThrottledDeviceReplansAndFoldsASmoothedThroughputIntoTheSharedProfiles)
    {
        const std::shared_ptr<ProfileStore> store = speed_store({1.0, 1.0});
        LivePlanSource source(store, runtime_factories(), K_DEFAULT_REBALANCE_MARGIN);
        const PartitionPlan in_force = source.first(independent_nodes(2, 2)).value();
        PartitionMeasurement measured;
        measured.step_seconds = 2.5;
        measured.device_seconds[host_key(1)] = 0.5;
        measured.device_seconds[host_key(2)] = 2.5;
        const PartitionReport judged =
            make_partition_report(in_force.problem, *in_force.model, in_force.answer, measured);

        const HorizonOutcome outcome =
            source.horizon(in_force, judged, step_dispatches({{host_key(1), 0.5}, {host_key(2), 2.5}}), 1);

        EXPECT_EQ(outcome.action, RebalanceAction::Replan);
        ASSERT_TRUE(outcome.plan.has_value());
        // The throttled device moves to 1600 operations a second, so a node there still beats two nodes on host_key(1).
        EXPECT_NE(outcome.plan->answer.value().node_devices[0], outcome.plan->answer.value().node_devices[1]);
        EXPECT_EQ(store->model()->profiles().find(host_key(2))->base_throughput, smoothed_throughput(2000.0, 400.0));
    }

    TEST(Small_LivePlanSource, ASourcePlansWithWhatAnotherSessionFoldedAfterItWasBuilt)
    {
        const std::shared_ptr<ProfileStore> store = speed_store({1.0, 1.0});
        LivePlanSource second(store, runtime_factories(), K_DEFAULT_REBALANCE_MARGIN);
        // Another session's horizon folds a throttled host_key(2) into the shared profiles.
        static_cast<void>(store->fold(step_dispatches({{host_key(2), 2.5}})));

        const Result<PartitionPlan> plan = second.first(independent_nodes(2, 2));

        ASSERT_TRUE(plan.has_value());
        EXPECT_EQ(plan.value().model, store->model());
        EXPECT_NE(plan.value().answer.value().node_devices[0], plan.value().answer.value().node_devices[1]);
        EXPECT_EQ(plan.value().model->profiles().find(host_key(2))->base_throughput,
                  smoothed_throughput(2000.0, 400.0));
    }
} // namespace
