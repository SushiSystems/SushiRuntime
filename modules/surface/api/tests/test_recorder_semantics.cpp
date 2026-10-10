/**************************************************************************/
/* test_recorder_semantics.cpp                                            */
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
 * @file test_recorder_semantics.cpp
 * @brief Tests what a rule, a name, a move, a region access and a liveness observer mean.
 * @see modules/surface/api/tests/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

#include <gtest/gtest.h>

#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

static_assert(!std::is_move_constructible<API::Region>::value, "a Region must not move");
static_assert(!std::is_move_assignable<API::Region>::value, "a Region must not move-assign");
static_assert(!std::is_copy_constructible<API::Region>::value, "a Region must not copy");
static_assert(std::is_nothrow_move_constructible<API::Graph>::value, "a Graph must move");
static_assert(std::is_nothrow_move_constructible<API::DynamicGraph>::value,
              "a DynamicGraph must move");

namespace
{
    constexpr std::size_t WIDTH = 32;
    constexpr std::size_t RULE_NODES = 3;
    constexpr std::uint64_t CELL = 7;

    /** @brief Allocates WIDTH words and zeroes them. */
    API::Buffer<std::uint32_t> zeroed(Runtime& rt)
    {
        auto buffer = rt.buffer<std::uint32_t>(WIDTH);
        for (std::size_t i = 0; i < WIDTH; ++i)
            buffer[i] = 0;
        return buffer;
    }

    /** @brief Adds one rule that adds step to every word of target. */
    template <class Builder>
    void add_increment(Builder& builder, const API::Buffer<std::uint32_t>& target,
                       std::uint32_t step)
    {
        std::uint32_t* words = target.data();
        builder.add(Reads(target), Writes(target), WIDTH,
                    [words, step](std::size_t i) { words[i] += step; });
    }

    /**
     * @brief Records RULE_NODES kernels in one rule scope, each adding one to target.
     * @param target Holds WIDTH words; must outlive the recorder.
     */
    void record_three_node_rule(API::Recorder& recorder, const API::Buffer<std::uint32_t>& target)
    {
        std::uint32_t* words = target.data();
        auto rule = recorder.rule();
        for (std::size_t node = 0; node < RULE_NODES; ++node)
            recorder.kernel(WIDTH, API::Access(Reads(target), Writes(target)),
                            [words](std::size_t i) { words[i] += 1u; });
    }

    /** @brief Counts the node timing rows of report that carry name. */
    std::size_t rows_named(const Core::RunReport& report, const std::string& name)
    {
        std::size_t rows = 0;
        for (const Core::NodeTiming& row : report.node_timings)
            rows += row.name == name ? 1u : 0u;
        return rows;
    }
} // namespace

TEST(Integration_RecorderSemantics, ReadingARegionBetweenRunsDoesNotRecompose)
{
    auto rt = Runtime::create();
    auto values = zeroed(rt);
    auto world = rt.dynamic_graph();
    add_increment(world.region(CELL), values, 1u);

    world.run();
    ASSERT_EQ(world.compose_count(), 1u);

    EXPECT_EQ(world.region(CELL).size(), 1u);
    EXPECT_EQ(world.region(CELL).node_count(), 1u);
    world.run();

    EXPECT_EQ(world.compose_count(), 1u) << "reading a region must not rebuild it";
    EXPECT_EQ(values[0], 2u);
}

TEST(Integration_RecorderSemantics, ARecordingThroughAHeldRegionRecomposes)
{
    auto rt = Runtime::create();
    auto values = zeroed(rt);
    auto world = rt.dynamic_graph();
    API::Region& cell = world.region(CELL);
    add_increment(cell, values, 1u);
    world.run();
    ASSERT_EQ(world.compose_count(), 1u);

    add_increment(cell, values, 10u);
    world.run();

    EXPECT_EQ(world.compose_count(), 2u) << "a recording must rebuild the region it went to";
    EXPECT_EQ(values[0], 12u) << "the second run must execute both rules";
}

TEST(Integration_RecorderSemantics, ClearingARegionReplacesItsWork)
{
    auto rt = Runtime::create();
    auto values = zeroed(rt);
    auto world = rt.dynamic_graph();
    API::Region& cell = world.region(CELL);
    add_increment(cell, values, 1u);
    world.run();

    cell.clear();
    EXPECT_EQ(cell.size(), 0u);
    add_increment(cell, values, 100u);
    world.run();

    EXPECT_EQ(cell.size(), 1u);
    EXPECT_EQ(world.compose_count(), 2u);
    EXPECT_EQ(values[0], 101u) << "the cleared rule must not run again";
}

TEST(Integration_RecorderSemantics, ADropBetweenRunsRecomposes)
{
    auto rt = Runtime::create();
    auto kept = zeroed(rt);
    auto gone = zeroed(rt);
    auto world = rt.dynamic_graph();
    add_increment(world.region(CELL), kept, 1u);
    add_increment(world.region(CELL + 1), gone, 1u);
    world.run();

    world.drop(CELL + 1);
    world.run();

    EXPECT_EQ(world.compose_count(), 2u);
    EXPECT_EQ(kept[0], 2u);
    EXPECT_EQ(gone[0], 1u) << "a dropped region must not run again";
}

TEST(Integration_RecorderSemantics, AThreeNodeRuleIsOneRuleWithOneName)
{
    auto rt = Runtime::create();
    auto values = zeroed(rt);
    auto graph = rt.graph();
    record_three_node_rule(graph.recorder(), values);
    graph.name_last("pipeline");
    graph.set_profiling_requested(true);

    EXPECT_EQ(graph.size(), 1u);
    EXPECT_EQ(graph.node_count(), RULE_NODES);
    EXPECT_EQ(graph.last_rule(), 0u);

    Core::RunReport report;
    graph.run(1, report);

    // The nodes of a rule share one registration, and the report holds one row per registration.
    ASSERT_EQ(report.node_timings.size(), 1u);
    EXPECT_EQ(rows_named(report, "pipeline"), 1u) << "the rule's row must carry the rule's name";
    EXPECT_NE(graph.rule_registration(0), API::Graph::no_registration);
    EXPECT_EQ(graph.rule_registration(1), API::Graph::no_registration)
        << "the graph holds one rule, so position 1 names none";
    EXPECT_EQ(values[0], static_cast<std::uint32_t>(RULE_NODES));
}

TEST(Integration_RecorderSemantics, ARegionRuleReportsItsRegistrationAndName)
{
    auto rt = Runtime::create();
    auto first = zeroed(rt);
    auto second = zeroed(rt);
    auto world = rt.dynamic_graph();
    world.set_profiling_requested(true);
    add_increment(world.region(CELL), first, 1u);
    record_three_node_rule(world.region(CELL + 1).recorder(), second);
    world.region(CELL + 1).name_last("pipeline");

    EXPECT_EQ(world.size(), 2u);
    EXPECT_EQ(world.node_count(), 1u + RULE_NODES);
    EXPECT_EQ(world.rule_registration(CELL + 1, 0), API::DynamicGraph::no_registration);

    Core::RunReport report;
    world.run(1, report);

    EXPECT_EQ(rows_named(report, "pipeline"), 1u);
    EXPECT_EQ(world.rule_registration(CELL, 0), 0u);
    EXPECT_EQ(world.rule_registration(CELL + 1, 0), 1u)
        << "the three nodes of the second region's rule share the index after the first";
    EXPECT_EQ(world.rule_registration(CELL + 1, 1), API::DynamicGraph::no_registration);
    EXPECT_EQ(world.rule_registration(CELL + 2, 0), API::DynamicGraph::no_registration);
}

TEST(Integration_RecorderSemantics, NameLastOwnsItsText)
{
    auto rt = Runtime::create();
    auto values = zeroed(rt);
    auto graph = rt.graph();
    add_increment(graph, values, 1u);
    {
        std::string label = "integrate_velocities_of_the_seventh_cell";
        graph.name_last(label);
        label.assign(label.size(), '#');
    }
    add_increment(graph, values, 1u);
    graph.name_last(std::string("a label that is ") + "replaced by the next call");
    graph.name_last(std::string("integrate_") + std::to_string(CELL));
    graph.set_profiling_requested(true);

    Core::RunReport report;
    graph.run(1, report);

    ASSERT_EQ(report.node_timings.size(), 2u);
    EXPECT_EQ(rows_named(report, "integrate_velocities_of_the_seventh_cell"), 1u)
        << "the name must survive the string it was read from";
    EXPECT_EQ(rows_named(report, "integrate_7"), 1u)
        << "a temporary must be a valid argument, and the last name given wins";
}

TEST(Integration_RecorderSemantics, ANullOrEmptyNameLeavesTheDefault)
{
    auto rt = Runtime::create();
    auto values = zeroed(rt);
    auto graph = rt.graph();
    add_increment(graph, values, 1u);
    graph.set_profiling_requested(true);
    Core::RunReport unnamed;
    graph.run(1, unnamed);
    ASSERT_EQ(unnamed.node_timings.size(), 1u);

    const char* absent = nullptr;
    graph.name_last("named").name_last(absent);
    Core::RunReport report;
    graph.run(1, report);

    ASSERT_EQ(report.node_timings.size(), 1u);
    EXPECT_EQ(report.node_timings[0].name, unnamed.node_timings[0].name);
}

TEST(Integration_RecorderSemantics, MovedFromGraphIsEmpty)
{
    auto rt = Runtime::create();
    auto values = zeroed(rt);
    auto graph = rt.graph();
    add_increment(graph, values, 1u);
    graph.run();
    ASSERT_EQ(graph.compile_count(), 1u);

    API::Graph taken = std::move(graph);

    EXPECT_EQ(graph.size(), 0u);
    EXPECT_EQ(graph.node_count(), 0u);
    EXPECT_EQ(graph.compile_count(), 0u);
    EXPECT_EQ(graph.rule_registration(0), API::Graph::no_registration);
    auto refused = graph.try_run();
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().code, Errc::invalid_graph);

    EXPECT_EQ(taken.size(), 1u);
    taken.run();
    EXPECT_EQ(taken.compile_count(), 1u) << "the plan moves with the graph";
    EXPECT_EQ(values[0], 2u);

    add_increment(graph, values, 10u);
    graph.run();
    EXPECT_EQ(graph.size(), 1u);
    EXPECT_EQ(values[0], 12u) << "a moved-from graph records and runs again";
}

TEST(Integration_RecorderSemantics, MovedFromDynamicGraphHasNoRegions)
{
    auto rt = Runtime::create();
    auto values = zeroed(rt);
    auto world = rt.dynamic_graph();
    add_increment(world.region(CELL), values, 1u);
    world.run();

    API::DynamicGraph taken = std::move(world);

    EXPECT_EQ(world.region_count(), 0u);
    EXPECT_EQ(world.size(), 0u);
    EXPECT_EQ(world.compose_count(), 0u);
    EXPECT_FALSE(world.try_run(std::size_t{1}));

    EXPECT_EQ(taken.region_count(), 1u);
    taken.run();
    EXPECT_EQ(taken.compose_count(), 1u) << "the composed plan moves with the graph";
    EXPECT_EQ(values[0], 2u);

    add_increment(taken.region(CELL), values, 10u);
    taken.run();
    EXPECT_EQ(taken.compose_count(), 2u) << "a region still reports to the graph that owns it";
    EXPECT_EQ(values[0], 13u);

    add_increment(world.region(CELL), values, 100u);
    world.run();
    EXPECT_EQ(values[0], 113u) << "a moved-from graph takes regions and runs again";
}

TEST(Integration_RecorderSemantics, OneObserverPerAllocation)
{
    constexpr std::size_t NODES = 16;

    auto rt = Runtime::create();
    auto shared = zeroed(rt);
    auto other = zeroed(rt);
    auto graph = rt.graph();
    for (std::size_t node = 0; node < NODES; ++node)
        add_increment(graph, shared, 1u);
    EXPECT_EQ(graph.recorder().watched_allocation_count(), 1u)
        << "sixteen nodes over one buffer need one observer";

    add_increment(graph, other, 1u);
    add_increment(graph, shared, 1u);
    EXPECT_EQ(graph.recorder().watched_allocation_count(), 2u);

    graph.run();
    EXPECT_EQ(shared[0], static_cast<std::uint32_t>(NODES + 1));
}

TEST(Integration_RecorderSemantics, ADroppedBufferIsStillDetected)
{
    auto rt = Runtime::create();
    auto kept = zeroed(rt);
    std::optional<API::Buffer<std::uint32_t>> gone(zeroed(rt));
    auto graph = rt.graph();
    add_increment(graph, *gone, 1u);
    for (std::size_t node = 0; node < 4; ++node)
        add_increment(graph, kept, 1u);
    add_increment(graph, *gone, 1u);
    ASSERT_EQ(graph.recorder().watched_allocation_count(), 2u);
    ASSERT_TRUE(graph.try_run());

    gone.reset();

    auto refused = graph.try_run();
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().code, Errc::dangling_resource);
}

TEST(Integration_RecorderSemantics, ADroppedBufferIsStillDetectedInARegion)
{
    auto rt = Runtime::create();
    auto kept = zeroed(rt);
    std::optional<API::Buffer<std::uint32_t>> gone(zeroed(rt));
    auto world = rt.dynamic_graph();
    add_increment(world.region(CELL), kept, 1u);
    add_increment(world.region(CELL + 1), *gone, 1u);
    add_increment(world.region(CELL + 1), *gone, 1u);
    ASSERT_TRUE(world.try_run(std::size_t{1}));

    gone.reset();

    auto refused = world.try_run(std::size_t{1});
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().code, Errc::dangling_resource);
}

