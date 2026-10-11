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

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/sushiruntime.hpp>

using namespace SushiRuntime;

static_assert(!std::is_move_constructible<Region>::value, "a Region must not move");
static_assert(!std::is_move_assignable<Region>::value, "a Region must not move-assign");
static_assert(!std::is_copy_constructible<Region>::value, "a Region must not copy");
static_assert(std::is_nothrow_move_constructible<Graph>::value, "a Graph must move");
static_assert(std::is_nothrow_move_constructible<DynamicGraph>::value,
              "a DynamicGraph must move");
static_assert(Internal::Graph::TaskMetadata::K_MAX_PARAMS == 8,
              "AnOffloadRuleTakesAtMostEightParameters spells the limit as eight literals");

namespace
{
    constexpr std::size_t K_WIDTH = 32;
    constexpr std::size_t K_RULE_NODES = 3;
    constexpr std::uint64_t K_CELL = 7;

    /** @brief Allocates K_WIDTH words and zeroes them. */
    Buffer<std::uint32_t> zeroed(Runtime& rt)
    {
        auto buffer = rt.buffer<std::uint32_t>(K_WIDTH);
        for (std::size_t i = 0; i < K_WIDTH; ++i)
            buffer[i] = 0;
        return buffer;
    }

    /** @brief Adds one rule that adds step to every word of target. */
    template <class Builder>
    void add_increment(Builder& builder, const Buffer<std::uint32_t>& target,
                       std::uint32_t step)
    {
        std::uint32_t* words = target.data();
        builder.add(K_WIDTH, Reads(target), Writes(target),
                    [words, step](std::size_t i) { words[i] += step; });
    }

    /**
     * @brief Records K_RULE_NODES kernels in one rule scope, each adding one to target.
     * @param target Holds K_WIDTH words; must outlive the recorder.
     */
    void record_three_node_rule(Recorder& recorder, const Buffer<std::uint32_t>& target)
    {
        std::uint32_t* words = target.data();
        auto rule = recorder.rule();
        for (std::size_t node = 0; node < K_RULE_NODES; ++node)
            recorder.kernel(K_WIDTH, Access(Reads(target), Writes(target)),
                            [words](std::size_t i) { words[i] += 1u; });
    }

    /** @brief Counts the node timing rows of report that carry name. */
    std::size_t rows_named(const Internal::Observability::RunReport& report, const std::string& name)
    {
        std::size_t rows = 0;
        for (const Internal::Observability::NodeTiming& row : report.node_timings)
            rows += row.name == name ? 1u : 0u;
        return rows;
    }

    /** @brief Returns the error of the SushiException call throws, or nothing when none. */
    template <class Call>
    std::optional<Error> refusal_of(Call&& call)
    {
        try
        {
            call();
        }
        catch (const SushiException& refused)
        {
            return refused.error();
        }
        return std::nullopt;
    }
} // namespace

TEST(Integration_RecorderSemantics, ReadingARegionBetweenRunsDoesNotRecompose)
{
    auto rt = Runtime::create();
    auto values = zeroed(rt);
    auto world = rt.dynamic_graph();
    add_increment(world.region(K_CELL), values, 1u);

    world.run();
    ASSERT_EQ(world.compose_count(), 1u);

    EXPECT_EQ(world.region(K_CELL).size(), 1u);
    EXPECT_EQ(world.region(K_CELL).node_count(), 1u);
    world.run();

    EXPECT_EQ(world.compose_count(), 1u) << "reading a region must not rebuild it";
    EXPECT_EQ(values[0], 2u);
}

TEST(Integration_RecorderSemantics, ARecordingThroughAHeldRegionRecomposes)
{
    auto rt = Runtime::create();
    auto values = zeroed(rt);
    auto world = rt.dynamic_graph();
    Region& cell = world.region(K_CELL);
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
    Region& cell = world.region(K_CELL);
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
    add_increment(world.region(K_CELL), kept, 1u);
    add_increment(world.region(K_CELL + 1), gone, 1u);
    world.run();

    world.drop(K_CELL + 1);
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
    graph.name("pipeline");
    graph.set_profiling(true);

    EXPECT_EQ(graph.size(), 1u);
    EXPECT_EQ(graph.node_count(), K_RULE_NODES);
    EXPECT_EQ(graph.last_rule(), 0u);

    Internal::Observability::RunReport report;
    graph.run(1, report);

    // The nodes of a rule share one registration, and the report holds one row per registration.
    ASSERT_EQ(report.node_timings.size(), 1u);
    EXPECT_EQ(rows_named(report, "pipeline"), 1u) << "the rule's row must carry the rule's name";
    EXPECT_NE(graph.rule_registration(0), Graph::K_NO_REGISTRATION);
    EXPECT_EQ(graph.rule_registration(1), Graph::K_NO_REGISTRATION)
        << "the graph holds one rule, so position 1 names none";
    EXPECT_EQ(values[0], static_cast<std::uint32_t>(K_RULE_NODES));
}

TEST(Integration_RecorderSemantics, ARegionRuleReportsItsRegistrationAndName)
{
    auto rt = Runtime::create();
    auto first = zeroed(rt);
    auto second = zeroed(rt);
    auto world = rt.dynamic_graph();
    world.set_profiling(true);
    add_increment(world.region(K_CELL), first, 1u);
    record_three_node_rule(world.region(K_CELL + 1).recorder(), second);
    world.region(K_CELL + 1).name("pipeline");

    EXPECT_EQ(world.size(), 2u);
    EXPECT_EQ(world.node_count(), 1u + K_RULE_NODES);
    EXPECT_EQ(world.rule_registration(K_CELL + 1, 0), DynamicGraph::K_NO_REGISTRATION);

    Internal::Observability::RunReport report;
    world.run(1, report);

    EXPECT_EQ(rows_named(report, "pipeline"), 1u);
    EXPECT_EQ(world.rule_registration(K_CELL, 0), 0u);
    EXPECT_EQ(world.rule_registration(K_CELL + 1, 0), 1u)
        << "the three nodes of the second region's rule share the index after the first";
    EXPECT_EQ(world.rule_registration(K_CELL + 1, 1), DynamicGraph::K_NO_REGISTRATION);
    EXPECT_EQ(world.rule_registration(K_CELL + 2, 0), DynamicGraph::K_NO_REGISTRATION);
}

TEST(Integration_RecorderSemantics, NameLastOwnsItsText)
{
    auto rt = Runtime::create();
    auto values = zeroed(rt);
    auto graph = rt.graph();
    add_increment(graph, values, 1u);
    {
        std::string label = "integrate_velocities_of_the_seventh_cell";
        graph.name(label);
        label.assign(label.size(), '#');
    }
    add_increment(graph, values, 1u);
    graph.name(std::string("a label that is ") + "replaced by the next call");
    graph.name(std::string("integrate_") + std::to_string(K_CELL));
    graph.set_profiling(true);

    Internal::Observability::RunReport report;
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
    graph.set_profiling(true);
    Internal::Observability::RunReport unnamed;
    graph.run(1, unnamed);
    ASSERT_EQ(unnamed.node_timings.size(), 1u);

    const char* absent = nullptr;
    graph.name("named").name(absent);
    Internal::Observability::RunReport report;
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

    Graph taken = std::move(graph);

    EXPECT_EQ(graph.size(), 0u);
    EXPECT_EQ(graph.node_count(), 0u);
    EXPECT_EQ(graph.compile_count(), 0u);
    EXPECT_EQ(graph.rule_registration(0), Graph::K_NO_REGISTRATION);
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
    add_increment(world.region(K_CELL), values, 1u);
    world.run();

    DynamicGraph taken = std::move(world);

    EXPECT_EQ(world.region_count(), 0u);
    EXPECT_EQ(world.size(), 0u);
    EXPECT_EQ(world.compose_count(), 0u);
    EXPECT_FALSE(world.try_run(std::size_t{1}));

    EXPECT_EQ(taken.region_count(), 1u);
    taken.run();
    EXPECT_EQ(taken.compose_count(), 1u) << "the composed plan moves with the graph";
    EXPECT_EQ(values[0], 2u);

    add_increment(taken.region(K_CELL), values, 10u);
    taken.run();
    EXPECT_EQ(taken.compose_count(), 2u) << "a region still reports to the graph that owns it";
    EXPECT_EQ(values[0], 13u);

    add_increment(world.region(K_CELL), values, 100u);
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
    std::optional<Buffer<std::uint32_t>> gone(zeroed(rt));
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
    std::optional<Buffer<std::uint32_t>> gone(zeroed(rt));
    auto world = rt.dynamic_graph();
    add_increment(world.region(K_CELL), kept, 1u);
    add_increment(world.region(K_CELL + 1), *gone, 1u);
    add_increment(world.region(K_CELL + 1), *gone, 1u);
    ASSERT_TRUE(world.try_run(std::size_t{1}));

    gone.reset();

    auto refused = world.try_run(std::size_t{1});
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().code, Errc::dangling_resource);
}

/** @brief Tests that a recorder moved from and recorded on again repeats no generation. */
TEST(Integration_RecorderSemantics, AMovedFromRecorderNeverRepeatsAGeneration)
{
    auto rt = Runtime::create();
    auto values = zeroed(rt);
    auto graph = rt.graph();
    add_increment(graph, values, 1u);
    graph.run();
    ASSERT_EQ(values[0], 1u);

    Recorder& source = graph.recorder();
    std::vector<std::uint64_t> seen{source.generation()};
    Recorder taken = std::move(source);
    EXPECT_EQ(taken.generation(), seen[0]) << "the work moved keeps the generation it had";
    EXPECT_EQ(std::count(seen.begin(), seen.end(), source.generation()), 0);
    seen.push_back(source.generation());

    add_increment(graph, values, 10u);
    EXPECT_EQ(std::count(seen.begin(), seen.end(), source.generation()), 0)
        << "a generation the plan was built at must not name the new rule";
    graph.run();
    EXPECT_EQ(values[0], 11u) << "the plan of the rule moved out must not run again";

    auto other = rt.graph();
    add_increment(other, values, 100u);
    const std::uint64_t before = taken.generation();
    ASSERT_LE(other.recorder().generation(), before);
    taken = std::move(other.recorder());
    EXPECT_GT(taken.generation(), before) << "a move assignment must not step back";
    EXPECT_EQ(taken.node_count(), 1u);
}

/** @brief Tests that clear() inside an extension leaves a recorder that records one rule. */
TEST(Integration_RecorderSemantics, AClearInsideAnExtensionStartsANewRule)
{
    auto rt = Runtime::create();
    auto graph = rt.graph();
    std::size_t first = 0;
    std::size_t second = 0;
    graph.extend([&](Recorder& recorder)
    {
        recorder.host(Access(), [&first] { ++first; });
        recorder.clear();
        recorder.host(Access(), [&second] { ++second; });
    });

    EXPECT_EQ(graph.size(), 1u);
    EXPECT_EQ(graph.node_count(), 1u);
    graph.run();

    EXPECT_EQ(first, 0u) << "the node cleared must not run";
    EXPECT_EQ(second, 1u);
}

/** @brief Tests that a task of two command groups runs after its producer and before its reader. */
TEST(Integration_RecorderSemantics, ATaskOfTwoCommandGroupsIsOrderedByItsAccess)
{
    auto rt = Runtime::create();
    auto source = zeroed(rt);
    auto staged = zeroed(rt);
    auto result = zeroed(rt);
    auto copied = zeroed(rt);
    std::uint32_t* s = source.data();
    std::uint32_t* t = staged.data();
    std::uint32_t* r = result.data();
    std::uint32_t* c = copied.data();

    auto graph = rt.graph();
    graph.add(K_WIDTH, Reads(), Writes(source),
              [s](std::size_t i) { s[i] = static_cast<std::uint32_t>(i) + 1u; });
    graph.add_task(Reads(source), Writes(staged, result),
                   [s, t, r](sycl::queue& queue, const std::vector<sycl::event>& after)
                   {
                       const sycl::event doubled = queue.submit([&](sycl::handler& h)
                       {
                           h.depends_on(after);
                           h.parallel_for(sycl::range<1>(K_WIDTH),
                                          [s, t](sycl::id<1> i) { t[i] = s[i] * 2u; });
                       });
                       return queue.submit([&](sycl::handler& h)
                       {
                           h.depends_on(doubled);
                           h.parallel_for(sycl::range<1>(K_WIDTH),
                                          [t, r](sycl::id<1> i) { r[i] = t[i] + 1u; });
                       });
                   });
    graph.add(K_WIDTH, Reads(result), Writes(copied),
              [r, c](std::size_t i) { c[i] = r[i] * 3u; });

    EXPECT_EQ(graph.size(), 3u);
    EXPECT_EQ(graph.node_count(), 3u);
    graph.run();

    for (std::size_t i = 0; i < K_WIDTH; ++i)
        EXPECT_EQ(copied[i], ((static_cast<std::uint32_t>(i) + 1u) * 2u + 1u) * 3u)
            << "element " << i;
}

/** @brief Tests that an offload rule with nine parameters is refused and one with eight kept. */
TEST(Integration_RecorderSemantics, AnOffloadRuleTakesAtMostEightParameters)
{
    auto rt = Runtime::create();
    auto values = zeroed(rt);
    auto field = rt.state<std::uint32_t>(K_WIDTH);
    std::uint32_t* words = values.data();
    const auto local = [words](std::size_t i) { words[i] += 1u; };
    const auto rule = [](std::size_t i, const std::uint32_t* current, std::uint32_t* next)
    {
        next[i] = current[i];
    };
    auto graph = rt.graph();

    const auto task_refusal = refusal_of([&]
    {
        graph.add("recorder.task"_op, K_WIDTH, Reads(values), Writes(values), local,
                  {1, 2, 3, 4, 5, 6, 7, 8, 9});
    });
    ASSERT_TRUE(task_refusal);
    EXPECT_EQ(task_refusal->code, Errc::invalid_graph);
    EXPECT_NE(task_refusal->message.find("9 parameters"), std::string::npos)
        << task_refusal->message;
    EXPECT_NE(task_refusal->message.find("at most 8"), std::string::npos)
        << task_refusal->message;

    const auto rule_refusal = refusal_of([&]
    {
        graph.add("recorder.rule"_op, field, rule, {1, 2, 3, 4, 5, 6, 7, 8, 9});
    });
    ASSERT_TRUE(rule_refusal);
    EXPECT_EQ(rule_refusal->code, Errc::invalid_graph);
    EXPECT_EQ(graph.size(), 0u) << "a refused rule must not be recorded";

    graph.add("recorder.task"_op, K_WIDTH, Reads(values), Writes(values), local,
              {1, 2, 3, 4, 5, 6, 7, 8});
    graph.add("recorder.rule"_op, field, rule, {1, 2, 3, 4, 5, 6, 7, 8});
    EXPECT_EQ(graph.size(), 2u);
}

/** @brief Tests that an emitter that adds no node leaves the rule before it its name. */
TEST(Integration_RecorderSemantics, AnEmitterThatAddsNothingLeavesTheRuleBeforeItAlone)
{
    auto rt = Runtime::create();
    auto values = zeroed(rt);
    auto graph = rt.graph();
    add_increment(graph, values, 1u);
    graph.name("integrate");
    graph.add_emitter([](Internal::Graph::TaskGraph&) {}).name("optional");
    graph.set_profiling(true);

    Internal::Observability::RunReport report;
    graph.run(1, report);

    EXPECT_EQ(graph.size(), 2u);
    EXPECT_EQ(rows_named(report, "integrate"), 1u) << "the first rule must keep its name";
    EXPECT_EQ(rows_named(report, "optional"), 0u);
    EXPECT_NE(graph.rule_registration(0), Graph::K_NO_REGISTRATION);
    EXPECT_EQ(graph.rule_registration(1), Graph::K_NO_REGISTRATION)
        << "a rule that added no node has no registration";
    EXPECT_EQ(values[0], 1u);
}
