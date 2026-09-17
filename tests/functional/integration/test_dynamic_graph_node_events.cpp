/**************************************************************************/
/* test_dynamic_graph_node_events.cpp                                    */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

// G1 task 6 coverage: DynamicGraph's per-graph profiling switch and its
// registration index, dense and ordered by ascending region key then add()
// order at every recompose (spec
// docs/superpowers/specs/2026-09-15-g1-node-event-records-design.md §D, §F.6;
// plan wave 4 task 6, spec §E item 8).

#include <cstddef>
#include <cstdint>
#include <string>

#include <gtest/gtest.h>

#include <SushiRuntime/SushiRuntime.h>

namespace SushiRuntime
{
    namespace API
    {
        namespace detail
        {
            // Test-only friend seam onto DynamicGraph::plan_ (DynamicGraph has no
            // public accessor for it), mirroring Graph's NodeEventRecordsTestAccess.
            struct DynamicGraphNodeEventsTestAccess
            {
                static const Execution::CompiledGraph& plan(const DynamicGraph& g) noexcept
                {
                    return g.plan_;
                }
            };
        }
    }
}

using namespace SushiRuntime;

namespace
{
    // Registration index of the one compiled node named @p name. plan.all_nodes
    // is only populated while the graph requests profiling (dynamic_graph.hpp's
    // commit()), which every test here relies on.
    std::uint32_t registration_index_of(const Execution::CompiledGraph& plan, const char* name)
    {
        const Graph::Node* found = nullptr;
        for (const auto& n : plan.all_nodes)
        {
            if (std::string(n->metadata.name) == name)
            {
                EXPECT_EQ(found, nullptr) << "Node name '" << name << "' is not unique.";
                found = n.get();
            }
        }
        EXPECT_NE(found, nullptr) << "No compiled node named '" << name << "'.";
        return found ? found->registration_index : 0u;
    }
} // namespace

// A graph that never requested profiling records nothing, whatever the
// runtime's own default.
TEST(Integration_DynamicGraphNodeEvents, NotRequestedRecordsNothing)
{
    constexpr std::size_t N = 32;

    auto rt = Runtime::create();
    rt.rebalancer(false);
    auto buf = rt.buffer<int>(N);
    int* p = buf.data();

    auto world = rt.dynamic_graph();
    EXPECT_FALSE(world.profiling_requested());
    world.region(0).add(API::Reads{}, API::Writes(buf), N,
                        [p](std::size_t i) { p[i] = static_cast<int>(i); });

    auto report = world.run();
    EXPECT_TRUE(report.node_events.empty());
}

// A requested DynamicGraph must actually produce records, not just a correctly
// stamped flag: it needs its own CompiledGraph::capture_profiling_state() call,
// since it has no capture_base_state() of its own to piggyback on.
TEST(Integration_DynamicGraphNodeEvents, RequestedGraphProducesRecordsMatchingRenumberedIndex)
{
    constexpr std::size_t N = 32;

    auto rt = Runtime::create();
    rt.rebalancer(false);
    auto a = rt.buffer<int>(N);
    auto b = rt.buffer<int>(N);
    int* pa = a.data();
    int* pb = b.data();

    auto world = rt.dynamic_graph();
    world.set_profiling_requested(true);
    world.region(0).add(API::Reads{}, API::Writes(a), N,
                        [pa](std::size_t i) { pa[i] = static_cast<int>(i); }).name_last("A");
    world.region(1).add(API::Reads{}, API::Writes(b), N,
                        [pb](std::size_t i) { pb[i] = static_cast<int>(i); }).name_last("B");

    auto report = world.run();
    ASSERT_FALSE(report.node_events.empty())
        << "a requested DynamicGraph must record device dispatches, not just stamp the flag";

    const auto& plan = API::detail::DynamicGraphNodeEventsTestAccess::plan(world);
    const std::uint32_t idx_a = registration_index_of(plan, "A");
    const std::uint32_t idx_b = registration_index_of(plan, "B");

    for (const auto& rec : report.node_events)
        EXPECT_TRUE(rec.node_index == idx_a || rec.node_index == idx_b)
            << "record node_index " << rec.node_index << " matches neither renumbered index";
}

// Two regions, each with more than one add() call: node_index is dense and
// ordered by ascending region key first, then add() order within a region.
TEST(Integration_DynamicGraphNodeEvents, IndexDenseAndOrderedByRegionThenAddOrder)
{
    constexpr std::size_t N = 16;

    auto rt = Runtime::create();
    rt.rebalancer(false);
    auto a = rt.buffer<int>(N);
    auto b = rt.buffer<int>(N);
    auto c = rt.buffer<int>(N);
    int* pa = a.data();
    int* pb = b.data();
    int* pc = c.data();

    auto world = rt.dynamic_graph();
    world.set_profiling_requested(true);

    // Region 5 is added first but keyed above region 2, so its two calls must
    // still land after region 2's one call once ordered by ascending key.
    world.region(5).add(API::Reads{}, API::Writes(a), N,
                        [pa](std::size_t i) { pa[i] = static_cast<int>(i); }).name_last("R5_first");
    world.region(5).add(API::Reads{}, API::Writes(b), N,
                        [pb](std::size_t i) { pb[i] = static_cast<int>(i); }).name_last("R5_second");
    world.region(2).add(API::Reads{}, API::Writes(c), N,
                        [pc](std::size_t i) { pc[i] = static_cast<int>(i); }).name_last("R2_only");

    world.run();

    const auto& plan = API::detail::DynamicGraphNodeEventsTestAccess::plan(world);
    const std::uint32_t idx_r2 = registration_index_of(plan, "R2_only");
    const std::uint32_t idx_r5_first = registration_index_of(plan, "R5_first");
    const std::uint32_t idx_r5_second = registration_index_of(plan, "R5_second");

    EXPECT_EQ(idx_r2, 0u) << "region 2 sorts before region 5 by key, whatever add() order";
    EXPECT_EQ(idx_r5_first, 1u);
    EXPECT_EQ(idx_r5_second, 2u);
}

// Dropping a region and re-adding it must not leave a gap in the index space:
// the next recompose renumbers every live region densely from zero.
TEST(Integration_DynamicGraphNodeEvents, IndexStaysDenseAfterDropAndReAdd)
{
    constexpr std::size_t N = 16;

    auto rt = Runtime::create();
    rt.rebalancer(false);
    auto a = rt.buffer<int>(N);
    auto b = rt.buffer<int>(N);
    int* pa = a.data();
    int* pb = b.data();

    auto world = rt.dynamic_graph();
    world.set_profiling_requested(true);

    world.region(0).add(API::Reads{}, API::Writes(a), N,
                        [pa](std::size_t i) { pa[i] = static_cast<int>(i); }).name_last("kept");
    world.region(1).add(API::Reads{}, API::Writes(b), N,
                        [pb](std::size_t i) { pb[i] = static_cast<int>(i); }).name_last("dropped_v1");
    world.run();

    world.drop(1);
    world.region(1).add(API::Reads{}, API::Writes(b), N,
                        [pb](std::size_t i) { pb[i] = static_cast<int>(i) * 2; }).name_last("dropped_v2");
    world.run();

    const auto& plan = API::detail::DynamicGraphNodeEventsTestAccess::plan(world);
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
    rt.rebalancer(false);
    auto buf = rt.buffer<int>(N);
    int* p = buf.data();

    auto world = rt.dynamic_graph();
    world.region(0).add(API::Reads{}, API::Writes(buf), N,
                        [p](std::size_t i) { p[i] += 1; });

    world.run();
    const std::size_t compiles_before = world.compile_count();

    world.set_profiling_requested(true);
    world.run();
    EXPECT_EQ(world.compile_count(), compiles_before + 1)
        << "the toggle must force exactly one recompose";

    const std::size_t compiles_after_toggle = world.compile_count();
    world.run();
    EXPECT_EQ(world.compile_count(), compiles_after_toggle)
        << "an unmutated run after the toggle must not recompile again";
}
