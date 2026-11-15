/**************************************************************************/
/* test_partition_report_builder.cpp                                      */
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
 * @file test_partition_report_builder.cpp
 * @brief Tests that make_partition_report() gives one row per device that matches the plan.
 * @see tests/engine/partitioning/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <cstddef>
#include <utility>

#include <SushiRuntime/partitioning/fill_plan.hpp>
#include <SushiRuntime/partitioning/list_scheduling_partitioner.hpp>
#include <SushiRuntime/partitioning/partition_model.hpp>
#include <SushiRuntime/partitioning/partition_report_builder.hpp>

#include "support/partition_problem_builder.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;

namespace
{
    PartitionAssignment split_assignment(const PartitionProblem& problem)
    {
        PartitionAssignment assignment;
        assignment.node_devices = {host_key(1), host_key(1), host_key(2)};
        assignment.step_time = 1.5;
        assignment.floor_taken = true;
        assignment.homes[problem.buffers[0].handle] = host_key(1);
        assignment.homes[problem.buffers[1].handle] = host_key(1);
        assignment.homes[problem.buffers[2].handle] = host_key(2);
        return assignment;
    }

    TEST(Small_PartitionReportBuilder, EachDeviceRowCountsItsNodesBytesAndModelledTime)
    {
        const PartitionProblem problem = chain(3, 100, 3);
        const auto model = speed_model({1.0, 2.0, 1.0});
        const Result<PartitionAssignment, PartitionRefusal> answer = split_assignment(problem);

        const PartitionReport report = make_partition_report(problem, *model, answer);

        ASSERT_EQ(report.rows.size(), 3u);
        EXPECT_FALSE(report.refused);
        EXPECT_TRUE(report.floor_taken);
        EXPECT_EQ(report.step_seconds, 1.5);
        EXPECT_EQ(report.rows[0].device, host_key(1));
        EXPECT_EQ(report.rows[0].nodes, 2u);
        EXPECT_EQ(report.rows[0].bytes, 200u);
        EXPECT_EQ(report.rows[0].modelled_seconds, 2.0 * model->node_time(host_key(1), problem.nodes[0].cost).value());
        EXPECT_EQ(report.rows[1].device, host_key(2));
        EXPECT_EQ(report.rows[1].nodes, 1u);
        EXPECT_EQ(report.rows[1].bytes, 100u);
        EXPECT_EQ(report.rows[1].modelled_seconds, model->node_time(host_key(2), problem.nodes[2].cost).value());
    }

    TEST(Small_PartitionReportBuilder, EachDeviceRowCarriesItsMeasuredTimeAndCopiesAndTheReportTheMeasuredStep)
    {
        const PartitionProblem problem = chain(3, 100, 3);
        const auto model = speed_model({1.0, 2.0, 1.0});
        const Result<PartitionAssignment, PartitionRefusal> answer = split_assignment(problem);
        PartitionMeasurement measured;
        measured.step_seconds = 0.75;
        measured.device_seconds[host_key(2)] = 0.25;
        measured.copies[host_key(2)].push_back(PartitionCopyRow{"pcie", 1.0, 100.0});

        const PartitionReport report = make_partition_report(problem, *model, answer, measured);

        ASSERT_EQ(report.rows.size(), 3u);
        EXPECT_EQ(report.measured_step_seconds, 0.75);
        EXPECT_EQ(report.rows[0].measured_seconds, 0.0);
        EXPECT_EQ(report.rows[1].measured_seconds, 0.25);
        ASSERT_EQ(report.rows[1].copies.size(), 1u);
        EXPECT_EQ(report.rows[1].copies[0].path, "pcie");
        EXPECT_EQ(report.rows[1].copies[0].bytes, 100.0);
    }

    TEST(Small_PartitionReportBuilder, TheReportCarriesTheFloorsModelledStepAndDevices)
    {
        const PartitionProblem problem = chain(3, 100, 2);
        const auto model = speed_model({1.0, 2.0});
        const Result<PartitionAssignment, PartitionRefusal> answer = PartitionAssignment{};
        Result<PartitionModel, PartitionRefusal> built = PartitionModel::build(problem, *model);
        ASSERT_TRUE(built.has_value());
        const std::optional<FloorPlan> floor = floor_plan(built.value());
        ASSERT_TRUE(floor.has_value());

        const PartitionReport report = make_partition_report(problem, *model, answer);

        EXPECT_EQ(report.floor_step_seconds, floor->step_time);
        EXPECT_EQ(report.floor_devices.size(), floor->devices.size());
    }

    TEST(Small_PartitionReportBuilder, ADeviceWithNoNodeHasARowOfZeros)
    {
        const PartitionProblem problem = chain(3, 100, 3);
        const auto model = speed_model({1.0, 1.0, 1.0});
        const Result<PartitionAssignment, PartitionRefusal> answer = split_assignment(problem);

        const PartitionReport report = make_partition_report(problem, *model, answer);

        ASSERT_EQ(report.rows.size(), 3u);
        EXPECT_EQ(report.rows[2].device, host_key(3));
        EXPECT_EQ(report.rows[2].nodes, 0u);
        EXPECT_EQ(report.rows[2].bytes, 0u);
        EXPECT_EQ(report.rows[2].modelled_seconds, 0.0);
        EXPECT_EQ(report.rows[2].shortfall_bytes, 0u);
    }

    TEST(Small_PartitionReportBuilder, ABufferWithAStatedDeviceCountsAtThatDevice)
    {
        PartitionProblem problem = chain(3, 100, 2);
        problem.buffers[2].device = host_key(2);
        const auto model = speed_model({1.0, 1.0});
        PartitionAssignment assignment = split_assignment(problem);
        assignment.homes.erase(problem.buffers[2].handle);
        const Result<PartitionAssignment, PartitionRefusal> answer = assignment;

        const PartitionReport report = make_partition_report(problem, *model, answer);

        EXPECT_EQ(report.rows[1].bytes, 100u);
    }

    TEST(Small_PartitionReportBuilder, ARefusalGivesTheSentenceAndTheShortfallOfEachPool)
    {
        const PartitionProblem problem = chain(3, 100, 2);
        const auto model = speed_model({1.0, 1.0});
        PartitionRefusal refusal{Error(ErrorCode::AllocationFailed, "the job fits no pool"), {}};
        refusal.pools = {PoolShortfall{1, 1500, 900}, PoolShortfall{2, 800, 900}};
        const Result<PartitionAssignment, PartitionRefusal> answer = make_error(std::move(refusal));

        const PartitionReport report = make_partition_report(problem, *model, answer);

        EXPECT_TRUE(report.refused);
        EXPECT_EQ(report.refusal, "the job fits no pool");
        ASSERT_EQ(report.rows.size(), 2u);
        EXPECT_EQ(report.rows[0].shortfall_bytes, 600u);
        EXPECT_EQ(report.rows[1].shortfall_bytes, 0u);
        EXPECT_EQ(report.rows[0].nodes, 0u);
        EXPECT_EQ(report.rows[0].bytes, 0u);
    }

    TEST(Small_PartitionReportBuilder, TheReportOfAPartitionerAnswerMatchesItsAssignment)
    {
        const PartitionProblem problem = chain(4, 100, 2);
        const auto model = speed_model({1.0, 1.0});
        const ListSchedulingPartitioner partitioner(model);
        const Result<PartitionAssignment, PartitionRefusal> answer = partitioner.partition(problem);
        ASSERT_TRUE(answer.has_value());

        const PartitionReport report = make_partition_report(problem, *model, answer);

        std::size_t nodes = 0;
        for (const PartitionDeviceRow& row : report.rows)
            nodes += row.nodes;
        EXPECT_EQ(nodes, problem.nodes.size());
        EXPECT_EQ(report.step_seconds, answer.value().step_time);
        for (const PartitionDeviceRow& row : report.rows)
        {
            std::size_t expected = 0;
            for (const DeviceKey& device : answer.value().node_devices)
                expected += device == row.device ? 1 : 0;
            EXPECT_EQ(row.nodes, expected);
        }
    }

    TEST(Small_PartitionReportBuilder, TheSameInputsGiveTheSameReport)
    {
        const PartitionProblem problem = chain(3, 100, 3);
        const auto model = speed_model({1.0, 2.0, 1.0});
        const Result<PartitionAssignment, PartitionRefusal> answer = split_assignment(problem);

        EXPECT_EQ(make_partition_report(problem, *model, answer).to_text(),
                  make_partition_report(problem, *model, answer).to_text());
    }
} // namespace
