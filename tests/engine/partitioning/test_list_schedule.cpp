/**************************************************************************/
/* test_list_schedule.cpp                                                 */
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
 * @file test_list_schedule.cpp
 * @brief Tests list_schedule(): its step is the model's step, it is deterministic and it stays bounded on sixteen devices.
 * @see tests/engine/partitioning/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <SushiRuntime/partitioning/assignment_hash.hpp>
#include <SushiRuntime/partitioning/fill_plan.hpp>
#include <SushiRuntime/partitioning/list_schedule.hpp>
#include <SushiRuntime/partitioning/list_scheduling_partitioner.hpp>

#include "random_partition_case.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;

namespace
{
    /// @brief Devices of the bounded case: the most a model takes.
    constexpr std::size_t K_WIDE_DEVICES = PartitionModel::K_MAX_DEVICES;

    /// @brief Nodes of the bounded case.
    constexpr std::size_t K_WIDE_NODES = 64;

    /// @brief Wall seconds the bounded case must partition within, on a debug build of the first machine.
    constexpr double K_WIDE_SECONDS = 5.0;

    /** @brief Returns a case of K_WIDE_NODES nodes over K_WIDE_DEVICES devices of seeded speeds. */
    [[nodiscard]] RandomPartitionCase wide_case(std::uint64_t seed)
    {
        SeededDraw draw(seed);
        const double table[] = {0.5, 1.0, 2.0, 4.0, 8.0};
        RandomPartitionCase made;
        for (std::size_t device = 0; device < K_WIDE_DEVICES; ++device)
            made.speeds.push_back(table[draw.between(0, 4)]);
        add_devices(made.problem, made.speeds.size());
        for (std::size_t node = 0; node < K_WIDE_NODES; ++node)
        {
            PartitionNode entry;
            entry.cost.operations = draw.between(1000, 9000);
            entry.writes.push_back(add_buffer(made.problem, draw.between(50, 100)));
            const std::uint64_t reads = node > 0 ? draw.between(1, 3) : 0;
            for (std::uint64_t read = 0; read < reads; ++read)
            {
                const std::size_t source = static_cast<std::size_t>(draw.between(0, node - 1));
                entry.reads.push_back(source);
                made.problem.edges.push_back(PartitionEdge{source, node, made.problem.buffers[source].bytes});
            }
            made.problem.nodes.push_back(entry);
        }
        return made;
    }

    /** @brief Names a seeded test after its seed. */
    [[nodiscard]] std::string seed_name(const ::testing::TestParamInfo<std::uint64_t>& info)
    {
        return "Seed" + std::to_string(info.param);
    }

    class Small_ListSchedule : public ::testing::TestWithParam<std::uint64_t>
    {
    };

    TEST_P(Small_ListSchedule, TheScheduledStepIsTheModelsStepOfThePlanInItsOrder)
    {
        const std::uint64_t seed = GetParam();
        const RandomPartitionCase made = random_partition_case(seed, {1.0, 2.0, 4.0}, 100);
        const auto costs = speed_model(made.speeds);
        Result<PartitionModel, PartitionRefusal> model = PartitionModel::build(made.problem, *costs);
        ASSERT_TRUE(model.has_value());

        const std::optional<ScheduledPlan> placed = place_by_rank(model.value());
        ASSERT_TRUE(placed.has_value());
        EXPECT_DOUBLE_EQ(placed->step_time, model.value().step_time(placed->placement, placed->order));

        const std::optional<ScheduledPlan> refined = list_schedule(model.value());
        ASSERT_TRUE(refined.has_value());
        EXPECT_DOUBLE_EQ(refined->step_time, model.value().step_time(refined->placement, refined->order));
        EXPECT_LE(refined->step_time, placed->step_time);
    }

    TEST_P(Small_ListSchedule, EqualInputsGiveEqualPlans)
    {
        const std::uint64_t seed = GetParam();
        const RandomPartitionCase made = random_partition_case(seed, {1.0, 1.0, 2.0}, 100);
        const auto costs = speed_model(made.speeds);
        const ListSchedulingPartitioner partitioner(costs);

        const Result<PartitionAssignment, PartitionRefusal> first = partitioner.partition(made.problem);
        const Result<PartitionAssignment, PartitionRefusal> second = partitioner.partition(made.problem);

        ASSERT_TRUE(first.has_value());
        ASSERT_TRUE(second.has_value());
        EXPECT_EQ(assignment_hash(first.value()), assignment_hash(second.value()));
    }

    INSTANTIATE_TEST_SUITE_P(Seeds, Small_ListSchedule, ::testing::Range<std::uint64_t>(1, 33), seed_name);

    TEST(Medium_ListSchedule, SixteenDevicesPartitionWithinTheirBoundAndDeterministically)
    {
        const RandomPartitionCase made = wide_case(3);
        const auto costs = speed_model(made.speeds);
        const ListSchedulingPartitioner partitioner(costs);

        const auto start = std::chrono::steady_clock::now();
        const Result<PartitionAssignment, PartitionRefusal> first = partitioner.partition(made.problem);
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        const Result<PartitionAssignment, PartitionRefusal> second = partitioner.partition(made.problem);

        ASSERT_TRUE(first.has_value());
        ASSERT_TRUE(second.has_value());
        EXPECT_LT(seconds, K_WIDE_SECONDS);
        EXPECT_EQ(assignment_hash(first.value()), assignment_hash(second.value()));
        Result<PartitionModel, PartitionRefusal> model = PartitionModel::build(made.problem, *costs);
        ASSERT_TRUE(model.has_value());
        const std::optional<FloorPlan> floor = floor_plan(model.value());
        ASSERT_TRUE(floor.has_value());
        EXPECT_LE(first.value().step_time, floor->step_time);
    }
} // namespace
