/**************************************************************************/
/* test_partition_properties.cpp                                          */
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
 * @file test_partition_properties.cpp
 * @brief Tests that the partitioner stays within 10% of the exhaustive best, never models slower than its floor and gives a thousand-times-slower device no node.
 * @see tests/engine/coherence/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include <SushiRuntime/coherence/fill_plan.hpp>
#include <SushiRuntime/coherence/list_scheduling_partitioner.hpp>

#include "../random_partition_case.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;

namespace
{
    /// @brief The margin the step may sit above the best placement, from docs/design/GLOBAL_HANDLE/PARTITIONING.md.
    constexpr double K_SEARCH_MARGIN = 0.10;

    /// @brief Bytes of the largest buffer of a case whose buffers all fit one round_profile pool.
    constexpr std::uint64_t K_ROOMY_BUFFER_BYTES = 100;

    /// @brief Speed of a device a thousand times slower than the unit speed.
    constexpr double K_SLOW_SPEED = 0.001;

    /** @brief Returns the speeds of the case of seed: two or three devices with speeds from half to eight. */
    [[nodiscard]] std::vector<double> speeds_of(std::uint64_t seed)
    {
        SeededDraw draw(seed * 7919U + 13U);
        const double table[] = {0.5, 1.0, 2.0, 4.0, 8.0};
        std::vector<double> speeds;
        const std::uint64_t count = draw.between(2, 3);
        for (std::uint64_t device = 0; device < count; ++device)
            speeds.push_back(table[draw.between(0, 4)]);
        return speeds;
    }

    /** @brief Names a seeded test after its seed. */
    [[nodiscard]] std::string seed_name(const ::testing::TestParamInfo<std::uint64_t>& info)
    {
        return "Seed" + std::to_string(info.param);
    }

    class Small_PartitionProperties : public ::testing::TestWithParam<std::uint64_t>
    {
    };

    TEST_P(Small_PartitionProperties, StepTimeIsWithinTenPercentOfTheExhaustiveBest)
    {
        const std::uint64_t seed = GetParam();
        SCOPED_TRACE("seed " + std::to_string(seed));
        const RandomPartitionCase made = random_partition_case(seed, speeds_of(seed), K_ROOMY_BUFFER_BYTES);
        const auto costs = speed_model(made.speeds);
        const ListSchedulingPartitioner partitioner(costs);

        const Result<PartitionAssignment, PartitionRefusal> result = partitioner.partition(made.problem);
        Result<PartitionModel, PartitionRefusal> model = PartitionModel::build(made.problem, *costs);

        ASSERT_TRUE(result.has_value());
        ASSERT_TRUE(model.has_value());
        const std::optional<double> best = best_feasible_step_time(model.value());
        ASSERT_TRUE(best.has_value());
        const double chosen = model.value().step_time(placement_of(model.value(), result.value()));
        EXPECT_LE(chosen, *best * (1.0 + K_SEARCH_MARGIN));
    }

    TEST_P(Small_PartitionProperties, NoPlanIsModelledSlowerThanItsFloor)
    {
        const std::uint64_t seed = GetParam();
        SCOPED_TRACE("seed " + std::to_string(seed));
        const RandomPartitionCase made = random_partition_case(seed, speeds_of(seed), K_ROOMY_BUFFER_BYTES);
        const auto costs = speed_model(made.speeds);
        const ListSchedulingPartitioner partitioner(costs);

        const Result<PartitionAssignment, PartitionRefusal> result = partitioner.partition(made.problem);
        Result<PartitionModel, PartitionRefusal> model = PartitionModel::build(made.problem, *costs);

        ASSERT_TRUE(result.has_value());
        ASSERT_TRUE(model.has_value());
        const std::optional<FloorPlan> floor = floor_plan(model.value());
        ASSERT_TRUE(floor.has_value());
        EXPECT_LE(result.value().step_time, floor->step_time);
    }

    TEST_P(Small_PartitionProperties, ADeviceAThousandTimesSlowerGetsNoNode)
    {
        const std::uint64_t seed = GetParam();
        SCOPED_TRACE("seed " + std::to_string(seed));
        std::vector<double> speeds = speeds_of(seed);
        speeds.push_back(K_SLOW_SPEED);
        const RandomPartitionCase made = random_partition_case(seed, speeds, K_ROOMY_BUFFER_BYTES);
        const ListSchedulingPartitioner partitioner(speed_model(made.speeds));
        const DeviceKey slow = host_key(static_cast<std::uint32_t>(made.speeds.size()));

        const Result<PartitionAssignment, PartitionRefusal> result = partitioner.partition(made.problem);

        ASSERT_TRUE(result.has_value());
        const std::vector<DeviceKey>& devices = result.value().node_devices;
        EXPECT_EQ(std::count(devices.begin(), devices.end(), slow), 0);
    }

    INSTANTIATE_TEST_SUITE_P(Seeds, Small_PartitionProperties, ::testing::Range<std::uint64_t>(1, 65), seed_name);
} // namespace
