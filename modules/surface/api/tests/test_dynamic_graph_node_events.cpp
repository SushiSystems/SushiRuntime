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
/* Licensed under the PolyForm Noncommercial License 1.0.0 (the           */
/* "License"); you may not use this file except in compliance with the    */
/* License. You may obtain a copy of the License at                       */
/*                                                                        */
/*     https://polyformproject.org/licenses/noncommercial/1.0.0           */
/*                                                                        */
/* Noncommercial use is free. Commercial use requires a separate licence  */
/* from Sushi Systems; see COMMERCIAL.md. The software is provided        */
/* "as is", without warranty of any kind.                                 */
/**************************************************************************/

/**
 * @file test_dynamic_graph_node_events.cpp
 * @brief Tests DynamicGraph's per-graph profiling switch and its dense registration index.
 * @see modules/surface/api/tests/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

#include <SushiRuntime/SushiRuntime.h>

#include "dynamic_graph_plan_access.hpp"

using namespace SushiRuntime;

namespace
{
    /** @brief Returns the registration index of the one compiled node with the given name. */
    std::uint32_t registration_index_of(const Internal::Graph::CompiledGraph& plan, const char* name)
    {
        const Internal::Graph::Node* found = Testing::find_plan_node(plan, name);
        return found ? found->registration_index : 0u;
    }
} // namespace

/** @brief Tests that a graph that never requested profiling records nothing. */
TEST(Integration_DynamicGraphNodeEvents, NotRequestedRecordsNothing)
{
    constexpr std::size_t N = 32;

    auto rt = Runtime::create();
    rt.set_rebalancer_enabled(false);
    auto buf = rt.buffer<int>(N);
    int* p = buf.data();

    auto world = rt.dynamic_graph();
    EXPECT_FALSE(world.is_profiling_requested());
    world.region(0).add(N, Reads{}, Writes(buf),
                        [p](std::size_t i) { p[i] = static_cast<int>(i); });

    auto report = world.run();
    EXPECT_TRUE(report.node_events.empty());
}

/** @brief Tests that a requested DynamicGraph produces records that match the renumbered index. */
TEST(Integration_DynamicGraphNodeEvents, RequestedGraphProducesRecordsMatchingRenumberedIndex)
{
    constexpr std::size_t N = 32;

    auto rt = Runtime::create();
    rt.set_rebalancer_enabled(false);
    auto a = rt.buffer<int>(N);
    auto b = rt.buffer<int>(N);
    int* pa = a.data();
    int* pb = b.data();

    auto world = rt.dynamic_graph();
    world.set_profiling(true);
    world.region(0).add(N, Reads{}, Writes(a),
                        [pa](std::size_t i) { pa[i] = static_cast<int>(i); }).name("A");
    world.region(1).add(N, Reads{}, Writes(b),
                        [pb](std::size_t i) { pb[i] = static_cast<int>(i); }).name("B");

    auto report = world.run();
    ASSERT_FALSE(report.node_events.empty())
        << "a requested DynamicGraph must record device dispatches, not just stamp the flag";

    const auto& plan = detail::DynamicGraphNodeEventsTestAccess::plan(world);
    const std::uint32_t idx_a = registration_index_of(plan, "A");
    const std::uint32_t idx_b = registration_index_of(plan, "B");

    for (const auto& rec : report.node_events)
        EXPECT_TRUE(rec.node_index == idx_a || rec.node_index == idx_b)
            << "record node_index " << rec.node_index << " matches neither renumbered index";
}

/** @brief Tests that node_index is dense and ordered by ascending region key, then add() order. */
TEST(Integration_DynamicGraphNodeEvents, IndexDenseAndOrderedByRegionThenAddOrder)
{
    constexpr std::size_t N = 16;

    auto rt = Runtime::create();
    rt.set_rebalancer_enabled(false);
    auto a = rt.buffer<int>(N);
    auto b = rt.buffer<int>(N);
    auto c = rt.buffer<int>(N);
    int* pa = a.data();
    int* pb = b.data();
    int* pc = c.data();

    auto world = rt.dynamic_graph();
    world.set_profiling(true);

    // Region 5 is keyed above region 2, so its two calls land after region 2's one; see README.md.
    world.region(5).add(N, Reads{}, Writes(a),
                        [pa](std::size_t i) { pa[i] = static_cast<int>(i); }).name("R5_first");
    world.region(5).add(N, Reads{}, Writes(b),
                        [pb](std::size_t i) { pb[i] = static_cast<int>(i); }).name("R5_second");
    world.region(2).add(N, Reads{}, Writes(c),
                        [pc](std::size_t i) { pc[i] = static_cast<int>(i); }).name("R2_only");

    world.run();

    const auto& plan = detail::DynamicGraphNodeEventsTestAccess::plan(world);
    const std::uint32_t idx_r2 = registration_index_of(plan, "R2_only");
    const std::uint32_t idx_r5_first = registration_index_of(plan, "R5_first");
    const std::uint32_t idx_r5_second = registration_index_of(plan, "R5_second");

    EXPECT_EQ(idx_r2, 0u) << "region 2 sorts before region 5 by key, whatever add() order";
    EXPECT_EQ(idx_r5_first, 1u);
    EXPECT_EQ(idx_r5_second, 2u);
}

/** @brief Tests that the index stays dense after a region is dropped and re-added. */
TEST(Integration_DynamicGraphNodeEvents, IndexStaysDenseAfterDropAndReAdd)
{
    constexpr std::size_t N = 16;

    auto rt = Runtime::create();
    rt.set_rebalancer_enabled(false);
    auto a = rt.buffer<int>(N);
    auto b = rt.buffer<int>(N);
    int* pa = a.data();
    int* pb = b.data();

    auto world = rt.dynamic_graph();
    world.set_profiling(true);

    world.region(0).add(N, Reads{}, Writes(a),
                        [pa](std::size_t i) { pa[i] = static_cast<int>(i); }).name("kept");
    world.region(1).add(N, Reads{}, Writes(b),
                        [pb](std::size_t i) { pb[i] = static_cast<int>(i); }).name("dropped_v1");
    world.run();

    world.drop(1);
    world.region(1).add(N, Reads{}, Writes(b),
                        [pb](std::size_t i) { pb[i] = static_cast<int>(i) * 2; }).name("dropped_v2");
    world.run();

    const auto& plan = detail::DynamicGraphNodeEventsTestAccess::plan(world);
    const std::uint32_t idx_kept = registration_index_of(plan, "kept");
    const std::uint32_t idx_v2 = registration_index_of(plan, "dropped_v2");

    EXPECT_EQ(world.region_count(), 2u);
    EXPECT_EQ(idx_kept, 0u);
    EXPECT_EQ(idx_v2, 1u) << "re-adding region 1 must not resume from a stale counter";
}

// Toggling the per-graph request recompiles exactly once, not once per run.
TEST(Integration_DynamicGraphNodeEvents, RequestToggleRecompilesOnce)
{
    constexpr std::size_t N = 8;

    auto rt = Runtime::create();
    rt.set_rebalancer_enabled(false);
    auto buf = rt.buffer<int>(N);
    int* p = buf.data();

    auto world = rt.dynamic_graph();
    world.region(0).add(N, Reads{}, Writes(buf),
                        [p](std::size_t i) { p[i] += 1; });

    world.run();
    const std::size_t compiles_before = world.compose_count();

    world.set_profiling(true);
    world.run();
    EXPECT_EQ(world.compose_count(), compiles_before + 1)
        << "the toggle must force exactly one recompose";

    const std::size_t compiles_after_toggle = world.compose_count();
    world.run();
    EXPECT_EQ(world.compose_count(), compiles_after_toggle)
        << "an unmutated run after the toggle must not recompile again";
}
