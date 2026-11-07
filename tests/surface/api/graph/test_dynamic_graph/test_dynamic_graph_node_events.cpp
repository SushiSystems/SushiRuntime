/**************************************************************************/
/* test_dynamic_graph_node_events.cpp                                     */
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
 * @file test_dynamic_graph_node_events.cpp
 * @brief Tests the per-graph profiling switch of the DynamicGraph and its dense registration index.
 * @see tests/surface/api/graph/README.md
 * @author Mustafa Garip
 */

#include <cstddef>

#include <gtest/gtest.h>

#include <SushiRuntime/sushiruntime.hpp>

#include "run_recorder.hpp"
#include "support/host_runtime.hpp"

using namespace SushiRuntime;

/** @brief Tests DynamicGraph's per-graph profiling switch and its dense registration index. */
namespace DynamicGraphNodeEventsTests
{
    namespace
    {
        constexpr std::size_t N = 16;

        /** @brief Records a region that fills one buffer with its index, under the given label. */
        void add_labelled_fill(DynamicGraph& world, DynamicGraph::RegionKey key, Buffer<int>& buffer,
                               int scale, const char* label)
        {
            int* p = buffer.data();
            world.region(key).add(N, Reads{}, Writes(buffer),
                                  [p, scale](std::size_t i) { p[i] = static_cast<int>(i) * scale; })
                .name(label);
        }
    } // namespace

    /** @brief Fixture of the node-event tests, which run on a host runtime. */
    class Small_DynamicGraphNodeEvents : public TestSupport::HostRuntimeTest
    {
    };

    /** @brief Tests that every record of an observed run names a node the table of the run describes. */
    TEST_F(Small_DynamicGraphNodeEvents, RequestedGraphProducesRecordsMatchingRenumberedIndex)
    {
        Testing::RunRecorder observed(runtime());
        auto a = runtime().buffer<int>(N).value();
        auto b = runtime().buffer<int>(N).value();

        auto world = runtime().dynamic_graph();
        add_labelled_fill(world, 0, a, 1, "A");
        add_labelled_fill(world, 1, b, 1, "B");

        world.run().value();
        ASSERT_EQ(observed.count(), 1u);
        const RunObservation observation = observed.last();
        ASSERT_FALSE(observation.events.empty())
            << "an observed DynamicGraph must record device dispatches, not just stamp the flag";
        ASSERT_NE(observation.table, nullptr);

        for (const auto& rec : observation.events)
        {
            const NodeTableRow* row = observation.table->find(rec.node_index);
            ASSERT_NE(row, nullptr) << "record node_index " << rec.node_index
                                    << " names no row of the table";
            EXPECT_TRUE(row->name == "A" || row->name == "B") << row->name;
        }
    }

    /** @brief Tests that the table indices are dense and ordered by ascending region key, then add() order. */
    TEST_F(Small_DynamicGraphNodeEvents, IndexDenseAndOrderedByRegionThenAddOrder)
    {
        Testing::RunRecorder observed(runtime());
        auto a = runtime().buffer<int>(N).value();
        auto b = runtime().buffer<int>(N).value();
        auto c = runtime().buffer<int>(N).value();

        auto world = runtime().dynamic_graph();

        // Region 5 is keyed above region 2, so its two calls land after region 2's one; see README.md.
        add_labelled_fill(world, 5, a, 1, "R5_first");
        add_labelled_fill(world, 5, b, 1, "R5_second");
        add_labelled_fill(world, 2, c, 1, "R2_only");

        world.run().value();
        ASSERT_EQ(observed.count(), 1u);
        const auto& rows = observed.last().table->rows();

        ASSERT_EQ(rows.size(), 3u);
        for (std::size_t k = 0; k < rows.size(); ++k)
            EXPECT_EQ(rows[k].index, k) << "the indices are not dense";
        EXPECT_EQ(rows[0].name, "R2_only") << "region 2 sorts before region 5 by key, whatever add() order";
        EXPECT_EQ(rows[1].name, "R5_first");
        EXPECT_EQ(rows[2].name, "R5_second");
    }

    /** @brief Tests that the index stays dense after a region is dropped and re-added. */
    TEST_F(Small_DynamicGraphNodeEvents, IndexStaysDenseAfterDropAndReAdd)
    {
        Testing::RunRecorder observed(runtime());
        auto a = runtime().buffer<int>(N).value();
        auto b = runtime().buffer<int>(N).value();

        auto world = runtime().dynamic_graph();

        add_labelled_fill(world, 0, a, 1, "kept");
        add_labelled_fill(world, 1, b, 1, "dropped_v1");
        world.run().value();

        world.drop(1);
        add_labelled_fill(world, 1, b, 2, "dropped_v2");
        world.run().value();

        ASSERT_EQ(observed.count(), 2u);
        const auto& rows = observed.last().table->rows();

        EXPECT_EQ(world.region_count(), 2u);
        ASSERT_EQ(rows.size(), 2u);
        EXPECT_EQ(rows[0].name, "kept");
        EXPECT_EQ(rows[0].index, 0u);
        EXPECT_EQ(rows[1].name, "dropped_v2");
        EXPECT_EQ(rows[1].index, 1u) << "re-adding region 1 must not resume from a stale counter";
    }
} // namespace DynamicGraphNodeEventsTests
