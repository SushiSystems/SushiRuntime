/**************************************************************************/
/* test_step_simulation.cpp                                               */
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
 * @file test_step_simulation.cpp
 * @brief Tests StepSimulation: one copy per range and pool, insertion into idle gaps, parts priced by copied bytes.
 * @see tests/engine/partitioning/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

#include <SushiRuntime/partitioning/partition_model.hpp>
#include <SushiRuntime/partitioning/planned_homes.hpp>
#include <SushiRuntime/partitioning/split_expansion.hpp>
#include <SushiRuntime/partitioning/split_sizing.hpp>
#include <SushiRuntime/partitioning/step_simulation.hpp>
#include <SushiRuntime/region/buffer_region.hpp>

#include "support/partition_problem_builder.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;

namespace
{
    /// @brief Seconds of a 1000-operation node at unit speed under round_profile.
    constexpr double K_NODE = 1000.0 / 2000.0;

    /// @brief Seconds of a 100-byte copy over round_profile's path.
    constexpr double K_COPY = 0.25 + 100.0 / 500.0;

    TEST(Small_StepSimulation, TwoReadersOfOneRangeInOnePoolPayOneCopy)
    {
        PartitionProblem problem = chain(2, 100, 2);
        PartitionNode second_reader;
        second_reader.cost.operations = 1000;
        second_reader.reads.push_back(0);
        second_reader.writes.push_back(add_buffer(problem, 100));
        problem.nodes.push_back(second_reader);
        problem.edges.push_back(PartitionEdge{0, 2, 100});
        const auto costs = speed_model({1.0, 1.0});
        Result<PartitionModel, PartitionRefusal> model = PartitionModel::build(problem, *costs);
        ASSERT_TRUE(model.has_value());

        StepSimulation simulation(model.value());
        simulation.commit(0, 0);
        simulation.commit(1, 1);
        simulation.commit(2, 1);

        EXPECT_EQ(simulation.copied_bytes(), 100U);
        EXPECT_DOUBLE_EQ(simulation.finish_of(1), 2.0 * K_NODE + K_COPY);
        EXPECT_DOUBLE_EQ(simulation.finish_of(2), 3.0 * K_NODE + K_COPY);
    }

    TEST(Small_StepSimulation, ANodeFillsAnIdleGapOfItsDevice)
    {
        PartitionProblem problem = chain(2, 100, 2);
        PartitionNode independent;
        independent.cost.operations = 1000;
        independent.writes.push_back(add_buffer(problem, 100));
        problem.nodes.push_back(independent);
        const auto costs = speed_model({1.0, 1.0});
        Result<PartitionModel, PartitionRefusal> model = PartitionModel::build(problem, *costs);
        ASSERT_TRUE(model.has_value());

        StepSimulation simulation(model.value());
        simulation.commit(0, 0);
        simulation.commit(1, 1);
        const double expected = simulation.finish_on(2, 1);
        simulation.commit(2, 1);

        EXPECT_DOUBLE_EQ(expected, K_NODE);
        EXPECT_DOUBLE_EQ(simulation.finish_of(2), K_NODE);
        EXPECT_DOUBLE_EQ(simulation.makespan(), 2.0 * K_NODE + K_COPY);
        EXPECT_DOUBLE_EQ(model.value().step_time({0, 1, 1}, {0, 1, 2}), simulation.makespan());
    }

    TEST(Small_StepSimulation, APlannedBufferReadBeforeItsWriteStartsAtThePlannedHome)
    {
        PartitionProblem problem;
        add_devices(problem, 2);
        const std::size_t carried = add_buffer(problem, 100);
        PartitionNode reader;
        reader.cost.operations = 1000;
        reader.reads.push_back(carried);
        reader.writes.push_back(add_buffer(problem, 100));
        PartitionNode writer;
        writer.cost.operations = 1000;
        writer.writes.push_back(carried);
        problem.nodes = {reader, writer};
        problem.edges.push_back(PartitionEdge{0, 1, 0});
        const auto costs = speed_model({1.0, 1.0});
        Result<PartitionModel, PartitionRefusal> model = PartitionModel::build(problem, *costs);
        ASSERT_TRUE(model.has_value());
        const std::vector<std::size_t> placement{0, 1};

        StepSimulation simulation(model.value(), placement);
        simulation.commit(0, 0);
        simulation.commit(1, 1);

        EXPECT_EQ(planned_homes(model.value(), placement).at(problem.buffers[carried].handle), host_key(2));
        EXPECT_EQ(simulation.copied_bytes(), 100U);
        EXPECT_DOUBLE_EQ(model.value().step_time(placement, {0, 1}), 2.0 * K_NODE + K_COPY);
    }

    TEST(Small_StepSimulation, ASplitPartPaysOnlyForTheBytesItReadsThatItsPoolLacks)
    {
        PartitionProblem problem = chain(2, 100, 2);
        problem.nodes[0].placed = host_key(1);
        const GlobalHandle input = problem.buffers[0].handle;
        const GlobalHandle output = problem.buffers[1].handle;
        PartitionSplit split{0, 2};
        split.regions = [input, output](const SplitPart& part)
        {
            SplitRegions regions;
            const auto begin = static_cast<std::size_t>(50 * part.begin);
            const auto length = static_cast<std::size_t>(50 * (part.end - part.begin));
            regions.reads.push_back(BufferRegion(input, begin, length));
            regions.writes.push_back(BufferRegion(output, begin, length));
            return regions;
        };
        problem.nodes[1].split = split;
        const auto costs = speed_model({1.0, 1.0});
        std::vector<std::vector<SizedPart>> parts(2);
        parts[1] = size_split(0, 2, {K_NODE, K_NODE});
        ASSERT_EQ(parts[1].size(), 2U);
        const SplitExpansion expansion = expand_splits(problem, parts, {host_key(1), host_key(2)});
        Result<PartitionModel, PartitionRefusal> model = PartitionModel::build(expansion.problem, *costs);
        ASSERT_TRUE(model.has_value());

        StepSimulation simulation(model.value());
        for (std::size_t node = 0; node < model.value().node_count(); ++node)
            simulation.commit(node, *model.value().fixed_device(node));

        EXPECT_EQ(simulation.copied_bytes(), 50U);
    }
} // namespace
