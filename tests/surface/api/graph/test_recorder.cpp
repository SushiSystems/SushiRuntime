/**************************************************************************/
/* test_recorder.cpp                                                      */
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
 * @file test_recorder.cpp
 * @brief Tests the Recorder: its semantics, its scratch memory and the indices of the rules it records.
 * @see tests/surface/api/graph/README.md
 * @author Mustafa Garip
 */

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/task/task_types.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/context_access/context_access.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/execution/execution_engine.hpp>
#include <SushiRuntime/graph/compiled_graph.hpp>
#include <SushiRuntime/graph/task_graph.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/task_work.hpp>

#include <SushiRuntime/engine_handles/plan_access.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        // Test-only friend seam onto the plan of a Graph, which has no public accessor.
        struct RegistrationIndexTestAccess
        {
            static const SushiRuntime::Internal::Graph::CompiledGraph& plan(const Graph& g) noexcept
            {
                return *engine_plan(g.compiled_plan());
            }
        };
    }
}

using namespace SushiRuntime;

/** @brief Tests what a rule, a name, a move, a region access and a liveness observer mean. */
namespace RecorderSemanticsTests
{
    static_assert(!std::is_move_constructible<Region>::value, "a Region must not move");
    static_assert(!std::is_move_assignable<Region>::value, "a Region must not move-assign");
    static_assert(!std::is_copy_constructible<Region>::value, "a Region must not copy");
    static_assert(!std::is_constructible<Region, ContextHandle>::value,
                  "a Region comes only from DynamicGraph::region()");
    static_assert(!std::is_move_constructible<Recorder>::value,
                  "only a Graph or a Region moves a Recorder");
    static_assert(!std::is_move_assignable<Recorder>::value,
                  "a Recorder must not be assigned into another");
    static_assert(!std::is_constructible<Recorder, ContextHandle>::value,
                  "only a Graph or a Region starts a Recorder");
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
            auto buffer = rt.buffer<std::uint32_t>(K_WIDTH).value();
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
        std::size_t rows_named(const RunReport& report, const std::string& name)
        {
            std::size_t rows = 0;
            for (const NodeTiming& row : report.node_timings)
                rows += row.name == name ? 1u : 0u;
            return rows;
        }

        /**
         * @brief Returns the refusal a call leaves on a recorder, or nothing when it leaves none.
         * @param recorder Cleared before the call and after the refusal is read.
         */
        template <class Call>
        std::optional<Error> refusal_of(Recorder& recorder, Call&& call)
        {
            recorder.clear();
            call();
            std::optional<Error> refused;
            if (recorder.refusal() != nullptr)
                refused = *recorder.refusal();
            recorder.clear();
            return refused;
        }
    } // namespace

    TEST(Medium_RecorderSemantics, ReadingARegionBetweenRunsDoesNotRecompose)
    {
        auto rt = Runtime::create().value();
        auto values = zeroed(rt);
        auto world = rt.dynamic_graph();
        add_increment(world.region(K_CELL), values, 1u);

        world.run().value();
        ASSERT_EQ(world.compose_count(), 1u);

        EXPECT_EQ(world.region(K_CELL).size(), 1u);
        EXPECT_EQ(world.region(K_CELL).node_count(), 1u);
        world.run().value();

        EXPECT_EQ(world.compose_count(), 1u) << "reading a region must not rebuild it";
        EXPECT_EQ(values[0], 2u);
    }

    TEST(Medium_RecorderSemantics, ARecordingThroughAHeldRegionRecomposes)
    {
        auto rt = Runtime::create().value();
        auto values = zeroed(rt);
        auto world = rt.dynamic_graph();
        Region& cell = world.region(K_CELL);
        add_increment(cell, values, 1u);
        world.run().value();
        ASSERT_EQ(world.compose_count(), 1u);

        add_increment(cell, values, 10u);
        world.run().value();

        EXPECT_EQ(world.compose_count(), 2u) << "a recording must rebuild the region it went to";
        EXPECT_EQ(values[0], 12u) << "the second run must execute both rules";
    }

    TEST(Medium_RecorderSemantics, ClearingARegionReplacesItsWork)
    {
        auto rt = Runtime::create().value();
        auto values = zeroed(rt);
        auto world = rt.dynamic_graph();
        Region& cell = world.region(K_CELL);
        add_increment(cell, values, 1u);
        world.run().value();

        cell.clear();
        EXPECT_EQ(cell.size(), 0u);
        add_increment(cell, values, 100u);
        world.run().value();

        EXPECT_EQ(cell.size(), 1u);
        EXPECT_EQ(world.compose_count(), 2u);
        EXPECT_EQ(values[0], 101u) << "the cleared rule must not run again";
    }

    TEST(Medium_RecorderSemantics, ADropBetweenRunsRecomposes)
    {
        auto rt = Runtime::create().value();
        auto kept = zeroed(rt);
        auto gone = zeroed(rt);
        auto world = rt.dynamic_graph();
        add_increment(world.region(K_CELL), kept, 1u);
        add_increment(world.region(K_CELL + 1), gone, 1u);
        world.run().value();

        world.drop(K_CELL + 1);
        world.run().value();

        EXPECT_EQ(world.compose_count(), 2u);
        EXPECT_EQ(kept[0], 2u);
        EXPECT_EQ(gone[0], 1u) << "a dropped region must not run again";
    }

    TEST(Medium_RecorderSemantics, AThreeNodeRuleIsOneRuleWithOneName)
    {
        auto rt = Runtime::create().value();
        auto values = zeroed(rt);
        auto graph = rt.graph();
        record_three_node_rule(graph.recorder(), values);
        graph.name("pipeline");
        graph.set_profiling(true);

        EXPECT_EQ(graph.size(), 1u);
        EXPECT_EQ(graph.node_count(), K_RULE_NODES);
        EXPECT_EQ(graph.last_rule(), 0u);

        RunReport report;
        graph.run(1, report).value();

        // The nodes of a rule share one registration, and the report holds one row per registration.
        ASSERT_EQ(report.node_timings.size(), 1u);
        EXPECT_EQ(rows_named(report, "pipeline"), 1u) << "the rule's row must carry the rule's name";
        EXPECT_NE(graph.rule_registration(0), Graph::K_NO_REGISTRATION);
        EXPECT_EQ(graph.rule_registration(1), Graph::K_NO_REGISTRATION)
            << "the graph holds one rule, so position 1 names none";
        EXPECT_EQ(values[0], static_cast<std::uint32_t>(K_RULE_NODES));
    }

    TEST(Medium_RecorderSemantics, ARegionRuleReportsItsRegistrationAndName)
    {
        auto rt = Runtime::create().value();
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

        RunReport report;
        world.run(1, report).value();

        EXPECT_EQ(rows_named(report, "pipeline"), 1u);
        EXPECT_EQ(world.rule_registration(K_CELL, 0), 0u);
        EXPECT_EQ(world.rule_registration(K_CELL + 1, 0), 1u)
            << "the three nodes of the second region's rule share the index after the first";
        EXPECT_EQ(world.rule_registration(K_CELL + 1, 1), DynamicGraph::K_NO_REGISTRATION);
        EXPECT_EQ(world.rule_registration(K_CELL + 2, 0), DynamicGraph::K_NO_REGISTRATION);
    }

    TEST(Medium_RecorderSemantics, NameLastOwnsItsText)
    {
        auto rt = Runtime::create().value();
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

        RunReport report;
        graph.run(1, report).value();

        ASSERT_EQ(report.node_timings.size(), 2u);
        EXPECT_EQ(rows_named(report, "integrate_velocities_of_the_seventh_cell"), 1u)
            << "the name must survive the string it was read from";
        EXPECT_EQ(rows_named(report, "integrate_7"), 1u)
            << "a temporary must be a valid argument, and the last name given wins";
    }

    TEST(Medium_RecorderSemantics, ANullOrEmptyNameLeavesTheDefault)
    {
        auto rt = Runtime::create().value();
        auto values = zeroed(rt);
        auto graph = rt.graph();
        add_increment(graph, values, 1u);
        graph.set_profiling(true);
        RunReport unnamed;
        graph.run(1, unnamed).value();
        ASSERT_EQ(unnamed.node_timings.size(), 1u);

        const char* absent = nullptr;
        graph.name("named").name(absent);
        RunReport report;
        graph.run(1, report).value();

        ASSERT_EQ(report.node_timings.size(), 1u);
        EXPECT_EQ(report.node_timings[0].name, unnamed.node_timings[0].name);
    }

    TEST(Medium_RecorderSemantics, MovedFromGraphIsEmpty)
    {
        auto rt = Runtime::create().value();
        auto values = zeroed(rt);
        auto graph = rt.graph();
        add_increment(graph, values, 1u);
        graph.run().value();
        ASSERT_EQ(graph.compile_count(), 1u);

        Graph taken = std::move(graph);

        EXPECT_EQ(graph.size(), 0u);
        EXPECT_EQ(graph.node_count(), 0u);
        EXPECT_EQ(graph.compile_count(), 0u);
        EXPECT_EQ(graph.rule_registration(0), Graph::K_NO_REGISTRATION);
        auto refused = graph.run();
        ASSERT_FALSE(refused);
        EXPECT_EQ(refused.error().code, ErrorCode::InvalidGraph);

        EXPECT_EQ(taken.size(), 1u);
        taken.run().value();
        EXPECT_EQ(taken.compile_count(), 1u) << "the plan moves with the graph";
        EXPECT_EQ(values[0], 2u);

        add_increment(graph, values, 10u);
        graph.run().value();
        EXPECT_EQ(graph.size(), 1u);
        EXPECT_EQ(values[0], 12u) << "a moved-from graph records and runs again";
    }

    TEST(Medium_RecorderSemantics, MovedFromDynamicGraphHasNoRegions)
    {
        auto rt = Runtime::create().value();
        auto values = zeroed(rt);
        auto world = rt.dynamic_graph();
        add_increment(world.region(K_CELL), values, 1u);
        world.run().value();

        DynamicGraph taken = std::move(world);

        EXPECT_EQ(world.region_count(), 0u);
        EXPECT_EQ(world.size(), 0u);
        EXPECT_EQ(world.compose_count(), 0u);
        EXPECT_FALSE(world.run(std::size_t{1}));

        EXPECT_EQ(taken.region_count(), 1u);
        taken.run().value();
        EXPECT_EQ(taken.compose_count(), 1u) << "the composed plan moves with the graph";
        EXPECT_EQ(values[0], 2u);

        add_increment(taken.region(K_CELL), values, 10u);
        taken.run().value();
        EXPECT_EQ(taken.compose_count(), 2u) << "a region still reports to the graph that owns it";
        EXPECT_EQ(values[0], 13u);

        add_increment(world.region(K_CELL), values, 100u);
        world.run().value();
        EXPECT_EQ(values[0], 113u) << "a moved-from graph takes regions and runs again";
    }

    TEST(Medium_RecorderSemantics, OneObserverPerAllocation)
    {
        constexpr std::size_t NODES = 16;

        auto rt = Runtime::create().value();
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

        graph.run().value();
        EXPECT_EQ(shared[0], static_cast<std::uint32_t>(NODES + 1));
    }

    TEST(Medium_RecorderSemantics, ADroppedBufferIsStillDetected)
    {
        auto rt = Runtime::create().value();
        auto kept = zeroed(rt);
        std::optional<Buffer<std::uint32_t>> gone(zeroed(rt));
        auto graph = rt.graph();
        add_increment(graph, *gone, 1u);
        for (std::size_t node = 0; node < 4; ++node)
            add_increment(graph, kept, 1u);
        add_increment(graph, *gone, 1u);
        ASSERT_EQ(graph.recorder().watched_allocation_count(), 2u);
        ASSERT_TRUE(graph.run());

        gone.reset();

        auto refused = graph.run();
        ASSERT_FALSE(refused);
        EXPECT_EQ(refused.error().code, ErrorCode::DanglingResource);
    }

    TEST(Medium_RecorderSemantics, ADroppedBufferIsStillDetectedInARegion)
    {
        auto rt = Runtime::create().value();
        auto kept = zeroed(rt);
        std::optional<Buffer<std::uint32_t>> gone(zeroed(rt));
        auto world = rt.dynamic_graph();
        add_increment(world.region(K_CELL), kept, 1u);
        add_increment(world.region(K_CELL + 1), *gone, 1u);
        add_increment(world.region(K_CELL + 1), *gone, 1u);
        ASSERT_TRUE(world.run(std::size_t{1}));

        gone.reset();

        auto refused = world.run(std::size_t{1});
        ASSERT_FALSE(refused);
        EXPECT_EQ(refused.error().code, ErrorCode::DanglingResource);
    }

    /** @brief Tests that the recorder of a graph moved from repeats no generation when recorded on. */
    TEST(Medium_RecorderSemantics, ARecorderWhoseGraphMovedNeverRepeatsAGeneration)
    {
        auto rt = Runtime::create().value();
        auto values = zeroed(rt);
        auto graph = rt.graph();
        add_increment(graph, values, 1u);
        graph.run().value();
        ASSERT_EQ(values[0], 1u);

        const Recorder& source = graph.recorder();
        std::vector<std::uint64_t> seen{source.generation()};
        Graph taken = std::move(graph);
        EXPECT_EQ(taken.recorder().generation(), seen[0])
            << "the work moved keeps the generation it had";
        EXPECT_EQ(std::count(seen.begin(), seen.end(), source.generation()), 0);
        seen.push_back(source.generation());

        add_increment(graph, values, 10u);
        EXPECT_EQ(std::count(seen.begin(), seen.end(), source.generation()), 0)
            << "a generation the plan was built at must not name the new rule";
        graph.run().value();
        EXPECT_EQ(values[0], 11u) << "the plan of the rule moved out must not run again";

        auto other = rt.graph();
        add_increment(other, values, 100u);
        const std::uint64_t before = taken.recorder().generation();
        ASSERT_LE(other.recorder().generation(), before);
        taken = std::move(other);
        EXPECT_GT(taken.recorder().generation(), before) << "a move assignment must not step back";
        EXPECT_EQ(taken.node_count(), 1u);
    }

    /** @brief Tests that Graph::clear() forgets the rules, their registrations and the refusal. */
    TEST(Medium_RecorderSemantics, GraphClearLeavesAGraphThatRecordsAndRunsAgain)
    {
        auto rt = Runtime::create().value();
        auto values = zeroed(rt);
        auto graph = rt.graph();
        add_increment(graph, values, 1u);
        graph.run().value();
        ASSERT_EQ(values[0], 1u);
        ASSERT_NE(graph.rule_registration(0), Graph::K_NO_REGISTRATION);

        graph.clear();
        EXPECT_EQ(graph.size(), 0u);
        EXPECT_EQ(graph.rule_registration(0), Graph::K_NO_REGISTRATION);
        const auto empty_run = graph.run();
        ASSERT_FALSE(empty_run);
        EXPECT_EQ(empty_run.error().code, ErrorCode::InvalidGraph);

        add_increment(graph, values, 10u);
        graph.run().value();
        EXPECT_EQ(values[0], 11u) << "the plan of the cleared rule must not run again";
    }

    /** @brief Tests that clear() inside an extension leaves a recorder that records one rule. */
    TEST(Medium_RecorderSemantics, AClearInsideAnExtensionStartsANewRule)
    {
        auto rt = Runtime::create().value();
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
        graph.run().value();

        EXPECT_EQ(first, 0u) << "the node cleared must not run";
        EXPECT_EQ(second, 1u);
    }

    /** @brief Tests that a task of two command groups runs after its producer and before its reader. */
    TEST(Medium_RecorderSemantics, ATaskOfTwoCommandGroupsIsOrderedByItsAccess)
    {
        auto rt = Runtime::create().value();
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
        graph.run().value();

        for (std::size_t i = 0; i < K_WIDTH; ++i)
            EXPECT_EQ(copied[i], ((static_cast<std::uint32_t>(i) + 1u) * 2u + 1u) * 3u)
                << "element " << i;
    }

    /** @brief Tests that a rule that names an operation is refused nine parameters and keeps eight. */
    TEST(Medium_RecorderSemantics, ANamedRuleTakesAtMostEightParameters)
    {
        auto rt = Runtime::create().value();
        auto values = zeroed(rt);
        auto field = rt.state<std::uint32_t>(K_WIDTH).value();
        std::uint32_t* words = values.data();
        const auto local = [words](std::size_t i) { words[i] += 1u; };
        const auto rule = [](std::size_t i, const std::uint32_t* current, std::uint32_t* next)
        {
            next[i] = current[i];
        };
        auto graph = rt.graph();

        const std::uint64_t nine[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
        const std::uint64_t eight[] = {1, 2, 3, 4, 5, 6, 7, 8};
        NodeOptions task_options;
        task_options.operation = "recorder.task"_op;
        NodeOptions rule_options;
        rule_options.operation = "recorder.rule"_op;

        task_options.parameters = nine;
        rule_options.parameters = nine;
        const auto task_refusal = refusal_of(graph.recorder(), [&]
        {
            graph.add(K_WIDTH, Reads(values), Writes(values), local, task_options);
        });
        ASSERT_TRUE(task_refusal);
        EXPECT_EQ(task_refusal->code, ErrorCode::InvalidGraph);
        EXPECT_NE(task_refusal->message.find("9 parameters"), std::string::npos)
            << task_refusal->message;
        EXPECT_NE(task_refusal->message.find("at most 8"), std::string::npos)
            << task_refusal->message;

        const auto rule_refusal = refusal_of(graph.recorder(), [&]
        {
            graph.add_rule(field, rule, rule_options);
        });
        ASSERT_TRUE(rule_refusal);
        EXPECT_EQ(rule_refusal->code, ErrorCode::InvalidGraph);
        EXPECT_EQ(graph.size(), 0u) << "a refused rule must not be recorded";

        task_options.parameters = eight;
        rule_options.parameters = eight;
        graph.add(K_WIDTH, Reads(values), Writes(values), local, task_options);
        graph.add_rule(field, rule, rule_options);
        EXPECT_EQ(graph.size(), 2u);
    }

    /** @brief Tests that an extension that records nothing adds no rule and no node. */
    TEST(Medium_RecorderSemantics, AnExtensionThatRecordsNothingAddsNoRule)
    {
        auto rt = Runtime::create().value();
        auto values = zeroed(rt);
        auto graph = rt.graph();
        add_increment(graph, values, 1u);
        graph.name("integrate");
        const std::uint64_t recorded = graph.recorder().generation();
        graph.extend([](Recorder&) {});
        graph.set_profiling(true);

        RunReport report;
        graph.run(1, report).value();

        EXPECT_EQ(graph.size(), 1u);
        EXPECT_EQ(graph.node_count(), 1u);
        EXPECT_EQ(graph.recorder().generation(), recorded) << "nothing recorded, nothing to rebuild";
        EXPECT_EQ(rows_named(report, "integrate"), 1u) << "the first rule must keep its name";
        EXPECT_NE(graph.rule_registration(0), Graph::K_NO_REGISTRATION);
        EXPECT_EQ(values[0], 1u);
    }
} // namespace RecorderSemanticsTests

/** @brief Tests the USM a Recorder hands out to a rule: its lifetime, replay and regions. */
namespace RecorderScratchTests
{
    namespace
    {
        constexpr std::size_t K_COUNT = 1000;

        /** @brief Returns what the staged rule writes for one input value. */
        std::uint32_t staged(std::uint32_t value)
        {
            return (value + 1u) * 2u;
        }

        /**
         * @brief Records one rule of two nodes that hand their values through recorder scratch.
         *
         * The first node writes in[i] + 1 into scratch and the second writes twice that into out.
         * @param in  Holds at least K_COUNT values; must outlive the recorder.
         * @param out Holds at least K_COUNT values; must outlive the recorder.
         */
        void record_staged_rule(Recorder& recorder, const Buffer<std::uint32_t>& in,
                                const Buffer<std::uint32_t>& out)
        {
            std::uint32_t* stage =
                recorder.scratch<std::uint32_t>(K_COUNT, in.device_index(), in.residency()).value();
            const BufferRegion stage_region{stage, 0, K_COUNT * sizeof(std::uint32_t)};
            const std::uint32_t* source = in.data();
            std::uint32_t* target = out.data();

            auto rule = recorder.rule();
            recorder.kernel(K_COUNT, Access(Reads(in), Writes(stage_region)),
                            [source, stage](std::size_t i) { stage[i] = source[i] + 1u; });
            recorder.kernel(K_COUNT, Access(Reads(stage_region), Writes(out)),
                            [stage, target](std::size_t i) { target[i] = stage[i] * 2u; });
        }

        /** @brief Fills a buffer with a pattern that differs per seed. */
        void fill(Buffer<std::uint32_t>& buffer, std::uint32_t seed)
        {
            for (std::size_t i = 0; i < K_COUNT; ++i)
                buffer[i] = static_cast<std::uint32_t>(i) * 3u + seed;
        }
    } // namespace

    /** @brief Tests that a consumer of a rule's output sees what passed through its scratch. */
    TEST(Medium_RecorderScratch, AConsumerSeesWhatPassedThroughTheScratch)
    {
        auto rt   = Runtime::create().value();
        auto in   = rt.buffer<std::uint32_t>(K_COUNT).value();
        auto out  = rt.buffer<std::uint32_t>(K_COUNT).value();
        auto copy = rt.buffer<std::uint32_t>(K_COUNT).value();
        fill(in, 5);
        for (std::size_t i = 0; i < K_COUNT; ++i)
        {
            out[i] = 0;
            copy[i] = 0;
        }
        const std::uint32_t* o = out.data();
        std::uint32_t* c = copy.data();

        auto g = rt.graph();
        record_staged_rule(g.recorder(), in, out);
        g.add(K_COUNT, Reads(out), Writes(copy), [o, c](std::size_t i) { c[i] = o[i] + 1u; });
        g.run().value();

        for (std::size_t i = 0; i < K_COUNT; ++i)
        {
            ASSERT_EQ(out[i], staged(in[i])) << "i = " << i;
            ASSERT_EQ(copy[i], staged(in[i]) + 1u) << "i = " << i;
        }
    }

    /** @brief Tests that each run of a replayed rule reads its input as it stands, in one compile. */
    TEST(Medium_RecorderScratch, EachRunReadsItsInputAsItStands)
    {
        auto rt  = Runtime::create().value();
        auto in  = rt.buffer<std::uint32_t>(K_COUNT).value();
        auto out = rt.buffer<std::uint32_t>(K_COUNT).value();

        auto g = rt.graph();
        record_staged_rule(g.recorder(), in, out);

        for (std::uint32_t step = 0; step < 4; ++step)
        {
            fill(in, step * 11u);
            g.run().value();
            for (std::size_t i = 0; i < K_COUNT; ++i)
                ASSERT_EQ(out[i], staged(in[i])) << "step " << step << ", i = " << i;
        }
        EXPECT_EQ(g.compile_count(), 1u);
    }

    /** @brief Tests that the scratch a kernel captured stays valid after the graph recompiles. */
    TEST(Medium_RecorderScratch, ScratchSurvivesARecompile)
    {
        auto rt   = Runtime::create().value();
        auto in   = rt.buffer<std::uint32_t>(K_COUNT).value();
        auto out  = rt.buffer<std::uint32_t>(K_COUNT).value();
        auto copy = rt.buffer<std::uint32_t>(K_COUNT).value();
        fill(in, 1);
        const std::uint32_t* o = out.data();
        std::uint32_t* c = copy.data();

        auto g = rt.graph();
        record_staged_rule(g.recorder(), in, out);
        g.run().value();
        for (std::size_t i = 0; i < K_COUNT; ++i)
            ASSERT_EQ(out[i], staged(in[i])) << "first plan, i = " << i;

        // A node recorded after a run makes the next run build a second plan.
        g.add(K_COUNT, Reads(out), Writes(copy), [o, c](std::size_t i) { c[i] = o[i]; });
        fill(in, 9);
        g.run().value();

        EXPECT_EQ(g.compile_count(), 2u);
        for (std::size_t i = 0; i < K_COUNT; ++i)
        {
            ASSERT_EQ(out[i], staged(in[i])) << "second plan, i = " << i;
            ASSERT_EQ(copy[i], staged(in[i])) << "second plan, i = " << i;
        }
    }

    /** @brief Tests that scratch returns to the runtime when the graph that recorded it dies. */
    TEST(Medium_RecorderScratch, ScratchIsFreedWithTheRecorder)
    {
        auto rt  = Runtime::create().value();
        auto in  = rt.buffer<std::uint32_t>(K_COUNT).value();
        auto out = rt.buffer<std::uint32_t>(K_COUNT).value();
        const Internal::Memory::USMAllocator& memory =
            SushiRuntime::Internal::ContextAccess::context(rt).memory();
        const std::size_t baseline = memory.live_count();

        {
            auto g = rt.graph();
            record_staged_rule(g.recorder(), in, out);
            EXPECT_EQ(memory.live_count(), baseline + 1u) << "the rule takes one scratch allocation";
        }

        EXPECT_EQ(memory.live_count(), baseline);
    }

    /** @brief Tests that regions record rules with scratch and keep it across a region rebuild. */
    TEST(Medium_RecorderScratch, RegionsRecordRulesWithScratch)
    {
        auto rt  = Runtime::create().value();
        auto in  = rt.buffer<std::uint32_t>(K_COUNT).value();
        auto mid = rt.buffer<std::uint32_t>(K_COUNT).value();
        auto out = rt.buffer<std::uint32_t>(K_COUNT).value();

        auto world = rt.dynamic_graph();
        record_staged_rule(world.region(1).recorder(), in, mid);
        record_staged_rule(world.region(2).recorder(), mid, out);

        for (std::uint32_t round = 0; round < 3; ++round)
        {
            fill(in, round * 7u);
            // Renaming a rule changes the recording, so the last round replays region 1.
            if (round == 2)
                world.region(1).name("staged");
            world.run().value();
            for (std::size_t i = 0; i < K_COUNT; ++i)
            {
                ASSERT_EQ(mid[i], staged(in[i])) << "round " << round << ", i = " << i;
                ASSERT_EQ(out[i], staged(staged(in[i]))) << "round " << round << ", i = " << i;
            }
        }
        EXPECT_EQ(world.region(1).node_count(), 2u);
        EXPECT_EQ(world.compose_count(), 2u) << "the rename must have rebuilt region 1 once";
    }

    /** @brief Tests that scratch no device can hold comes back as AllocationFailed. */
    TEST(Medium_RecorderScratch, AScratchRequestNoDeviceCanSatisfyReturnsAllocationFailed)
    {
        auto rt = Runtime::create().value();
        auto graph = rt.graph();
        // Half the address space in bytes: the count fits std::size_t and no device has the room.
        const std::size_t count = std::numeric_limits<std::size_t>::max() / 2;

        const auto refused =
            graph.recorder().scratch<std::uint8_t>(count, DeviceIndex{0}, Residency::Shared);
        ASSERT_FALSE(refused.has_value());
        EXPECT_EQ(refused.error().code, ErrorCode::AllocationFailed);

        const auto granted =
            graph.recorder().scratch<std::uint8_t>(16, DeviceIndex{0}, Residency::Shared);
        EXPECT_TRUE(granted.has_value());
    }
} // namespace RecorderScratchTests

/** @brief Tests Node::registration_index and that the nodes of one rule scope share one index. */
namespace RegistrationIndexTests
{
    using namespace SushiRuntime::Internal::Execution;
    using namespace SushiRuntime::Internal::Graph;

    namespace
    {
        /** @brief A no-op device node body; these tests check bookkeeping, not results. */
        TaskWork noop_work()
        {
            return [](sycl::handler&) {};
        }

        /** @brief Finds the one compiled node named @p name, failing if missing or duplicated. */
        const Node& find_named(const Internal::Graph::CompiledGraph& plan, const char* name)
        {
            const Node* found = nullptr;
            for (const auto& n : plan.all_nodes)
            {
                if (std::string(n->metadata.name) == name)
                {
                    EXPECT_EQ(found, nullptr) << "Node name '" << name << "' is not unique.";
                    found = n.get();
                }
            }
            EXPECT_NE(found, nullptr) << "No compiled node named '" << name << "'.";
            return *found;
        }

        constexpr std::size_t K_WIDTH = 64;
        constexpr std::size_t K_RULE_NODES = 3;

        /** @brief Holds the three buffers the grouped rules pass their values through. */
        struct Stages
        {
            Buffer<std::uint32_t> first;  /**< The rule's input. */
            Buffer<std::uint32_t> second; /**< What the rule's first kernel writes. */
            Buffer<std::uint32_t> third;  /**< The rule's output. */
        };

        /** @brief Allocates the stage buffers and seeds the first with its indices. */
        Stages make_stages(Runtime& rt)
        {
            Stages s{rt.buffer<std::uint32_t>(K_WIDTH).value(),
                     rt.buffer<std::uint32_t>(K_WIDTH).value(),
                     rt.buffer<std::uint32_t>(K_WIDTH).value()};
            for (std::size_t i = 0; i < K_WIDTH; ++i)
            {
                s.first[i] = static_cast<std::uint32_t>(i);
                s.second[i] = 0;
                s.third[i] = 0;
            }
            return s;
        }

        /**
         * @brief Records K_RULE_NODES kernels in one rule scope, each reading what the last wrote.
         * @param stages Buffers of K_WIDTH words; must outlive the recorder.
         */
        void record_grouped_rule(Recorder& recorder, const Stages& stages)
        {
            std::uint32_t* a = stages.first.data();
            std::uint32_t* b = stages.second.data();
            std::uint32_t* c = stages.third.data();

            auto rule = recorder.rule();
            recorder.kernel(K_WIDTH, Access(Reads(stages.first), Writes(stages.second)),
                            [a, b](std::size_t i) { b[i] = a[i] + 1u; });
            recorder.kernel(K_WIDTH, Access(Reads(stages.second), Writes(stages.third)),
                            [b, c](std::size_t i) { c[i] = b[i] * 2u; });
            recorder.kernel(K_WIDTH, Access(Reads(stages.third), Writes(stages.first)),
                            [c, a](std::size_t i) { a[i] = c[i]; });
        }

        /**
         * @brief Records a resolver, a device-sized kernel and a closing kernel in one rule scope.
         * @param stages Buffers of K_WIDTH words; must outlive the recorder.
         * @param count  Live width of the device-sized kernel; its capacity is at most K_WIDTH.
         */
        void record_late_bound_rule(Recorder& recorder, const Stages& stages,
                                    const LateCount& count)
        {
            std::uint32_t* live = recorder.scratch<std::uint32_t>(
                1, stages.first.device_index(), stages.first.residency()).value();
            const BufferRegion live_region{live, 0, sizeof(std::uint32_t)};
            std::uint32_t* a = stages.first.data();
            std::uint32_t* b = stages.second.data();
            std::uint32_t* c = stages.third.data();

            auto rule = recorder.rule();
            recorder.resolve(std::array<LateCount, 1>{count}, stages.first.device_index(),
                             Access(Reads(), Writes(live_region)),
                             [live](const std::array<std::size_t, 1>& value)
                             {
                                 live[0] = static_cast<std::uint32_t>(value[0]);
                             });
            recorder.kernel(DeviceSized{count.capacity(), live, nullptr},
                            Access(Reads(stages.first, live_region), Writes(stages.second)),
                            [a, b](std::size_t t) { b[t] = a[t] + 1u; });
            recorder.kernel(std::size_t{1},
                            Access(Reads(stages.second, live_region), Writes(stages.third)),
                            [b, c, live](std::size_t) { c[0] = b[0] + live[0]; });
        }

        /** @brief Holds what a compiled plan says about one grouped rule and its two neighbours. */
        struct Grouping
        {
            std::optional<std::uint32_t> rule_index; /**< The index every rule node carries. */
            std::size_t rule_nodes = 0;              /**< Nodes that are neither neighbour. */
            std::uint32_t before_index = 0;          /**< Index of the node named "before". */
            std::uint32_t after_index = 0;           /**< Index of the node named "after". */
            std::vector<std::uint32_t> all_indices;  /**< Index of every node of the plan. */
        };

        /** @brief Reads a plan's grouping and fails when two rule nodes carry different indices. */
        Grouping grouping_of(const Internal::Graph::CompiledGraph& plan)
        {
            Grouping g;
            for (const auto& n : plan.all_nodes)
            {
                g.all_indices.push_back(n->registration_index);
                const std::string name(n->metadata.name);
                if (name == "before")
                {
                    g.before_index = n->registration_index;
                    continue;
                }
                if (name == "after")
                {
                    g.after_index = n->registration_index;
                    continue;
                }

                ++g.rule_nodes;
                if (!g.rule_index.has_value())
                    g.rule_index = n->registration_index;
                EXPECT_EQ(*g.rule_index, n->registration_index)
                    << "every node of one rule scope must share one registration_index.";
            }
            return g;
        }
    }

    /**
     * @brief Verifies registration_index follows call order, not compiled order.
     *
     * Registers A, B, C in that order; compile() lists them C, B, A, and
     * registration_index must still read 0, 1, 2.
     */
    TEST(Medium_RegistrationIndex, NodeIndexIsRegistrationOrder)
    {
        TaskGraph graph;

        graph.add_node(noop_work());
        graph.name_last_node("A");
        graph.add_node(noop_work());
        graph.name_last_node("B");
        graph.add_node(noop_work());
        graph.name_last_node("C");

        Internal::Graph::CompiledGraph plan = graph.compile().value();

        ASSERT_EQ(plan.all_nodes.size(), 3u);
        // Proves the premise this test depends on: compiled order is reversed.
        ASSERT_EQ(std::string(plan.all_nodes[0]->metadata.name), "C")
            << "TaskGraph::compile() no longer pops its LIFO stack in reverse; "
               "this test's premise (compiled order != registration order) no "
               "longer holds.";

        EXPECT_EQ(find_named(plan, "A").registration_index, 0u);
        EXPECT_EQ(find_named(plan, "B").registration_index, 1u);
        EXPECT_EQ(find_named(plan, "C").registration_index, 2u);

    }

    /**
     * @brief Verifies a reserved index passed to several add_node() calls groups
     *        them under one registration_index, the mechanism a Recorder rule scope
     *        uses so every node of one rule reports the same node_index.
     */
    TEST(Medium_RegistrationIndex, ReservedIndexSharedAcrossCallsGroupsNodes)
    {
        TaskGraph graph;

        // Its own index: not part of the group below.
        graph.add_node(noop_work());
        graph.name_last_node("before");

        // Three nodes sharing one reserved index.
        const std::uint32_t token = graph.reserve_registration();
        graph.add_node(noop_work(), {}, token);
        graph.name_last_node("level0");
        graph.add_node(noop_work(), {}, token);
        graph.name_last_node("level1");
        graph.add_node(noop_work(), {}, token);
        graph.name_last_node("level2");

        // Must draw a fresh index, not the group's.
        graph.add_node(noop_work());
        graph.name_last_node("after");

        Internal::Graph::CompiledGraph plan = graph.compile().value();
        ASSERT_EQ(plan.all_nodes.size(), 5u);

        const std::uint32_t before = find_named(plan, "before").registration_index;
        const std::uint32_t level0 = find_named(plan, "level0").registration_index;
        const std::uint32_t level1 = find_named(plan, "level1").registration_index;
        const std::uint32_t level2 = find_named(plan, "level2").registration_index;
        const std::uint32_t after  = find_named(plan, "after").registration_index;

        EXPECT_EQ(level0, token);
        EXPECT_EQ(level1, token);
        EXPECT_EQ(level2, token);
        EXPECT_NE(before, token) << "the group must not reuse an unrelated prior index.";
        EXPECT_NE(after, token) << "a node past the group must draw a fresh index.";
        EXPECT_EQ(after, before + 2)
            << "'after' must be before+2: the group draws exactly one index.";
    }

    /**
     * @brief Verifies a Recorder rule scope groups every node recorded in it under
     *        one registration_index, through the real fluent API.
     */
    TEST(Medium_RegistrationIndex, RuleScopeGroupsItsNodesUnderOneIndex)
    {
        using Detail::RegistrationIndexTestAccess;

        auto rt = Runtime::create().value();
        Stages stages = make_stages(rt);

        auto g = rt.graph();
        g.add_untracked(std::size_t{1}, [](std::size_t) {}).name("before");
        record_grouped_rule(g.recorder(), stages);
        g.add_untracked(std::size_t{1}, [](std::size_t) {}).name("after");
        g.run().value();

        const Grouping grouping = grouping_of(RegistrationIndexTestAccess::plan(g));

        ASSERT_EQ(grouping.rule_nodes, K_RULE_NODES);
        ASSERT_TRUE(grouping.rule_index.has_value());
        EXPECT_NE(grouping.before_index, *grouping.rule_index);
        EXPECT_NE(grouping.after_index, *grouping.rule_index);
        EXPECT_EQ(grouping.after_index, grouping.before_index + 2)
            << "'after' must be before+2: the group draws exactly one index.";
    }

    /** @brief Tests that a rule scope's group survives a rebuild onto a fresh TaskGraph. */
    TEST(Medium_RegistrationIndex, RuleScopeGroupSurvivesRebuild)
    {
        using Detail::RegistrationIndexTestAccess;

        auto rt = Runtime::create().value();
        Stages stages = make_stages(rt);

        auto g = rt.graph();
        g.add_untracked(std::size_t{1}, [](std::size_t) {}).name("before");
        record_grouped_rule(g.recorder(), stages);
        g.run().value();

        // An add() after the first run dirties the graph, so every spec is replayed; see README.md.
        g.add_untracked(std::size_t{1}, [](std::size_t) {}).name("after");
        g.run().value();

        const Internal::Graph::CompiledGraph& plan = RegistrationIndexTestAccess::plan(g);
        Grouping grouping = grouping_of(plan);

        ASSERT_EQ(grouping.rule_nodes, K_RULE_NODES);
        ASSERT_TRUE(grouping.rule_index.has_value());
        EXPECT_NE(find_named(plan, "before").registration_index, *grouping.rule_index);
        EXPECT_NE(find_named(plan, "after").registration_index, *grouping.rule_index);

        std::vector<std::uint32_t>& all_indices = grouping.all_indices;
        std::sort(all_indices.begin(), all_indices.end());
        all_indices.erase(std::unique(all_indices.begin(), all_indices.end()), all_indices.end());
        for (std::size_t i = 0; i < all_indices.size(); ++i)
            EXPECT_EQ(all_indices[i], i) << "registration indices must be dense 0..n-1 after rebuild.";
    }

    /**
     * @brief Tests that a rule scope groups a late-count resolver, a device-sized kernel and a
     *        plain kernel under one index.
     */
    TEST(Medium_RegistrationIndex, RuleScopeGroupsResolverAndDeviceSizedNodesUnderOneIndex)
    {
        using Detail::RegistrationIndexTestAccess;

        auto rt = Runtime::create().value();
        Stages stages = make_stages(rt);

        const LateCount count = LateCount::from_host(K_WIDTH, []() { return K_WIDTH; });

        auto g = rt.graph();
        g.add_untracked(std::size_t{1}, [](std::size_t) {}).name("before");
        record_late_bound_rule(g.recorder(), stages, count);
        g.add_untracked(std::size_t{1}, [](std::size_t) {}).name("after");
        g.run().value();

        const Grouping grouping = grouping_of(RegistrationIndexTestAccess::plan(g));

        ASSERT_EQ(grouping.rule_nodes, K_RULE_NODES);
        ASSERT_TRUE(grouping.rule_index.has_value());
        EXPECT_NE(grouping.before_index, *grouping.rule_index);
        EXPECT_NE(grouping.after_index, *grouping.rule_index);
        EXPECT_EQ(grouping.after_index, grouping.before_index + 2)
            << "'after' must be before+2: the group draws exactly one index.";
    }
} // namespace RegistrationIndexTests

/** @brief Tests Graph::rule_registration, which maps a rule to its registration index. */
namespace RuleRegistrationTests
{
    namespace
    {
        constexpr std::size_t K_WIDTH = 64;
        constexpr std::size_t K_PLAIN_RULES = 3;
        constexpr std::size_t K_RULE_NODES = 3;

        /** @brief Holds one graph, the buffers it names and the position of the grouped rule. */
        struct Recorded
        {
            Graph graph;                  /**< The graph under test. */
            Buffer<float> values;         /**< The plain kernels' target. */
            Buffer<std::uint32_t> first;  /**< The rule's input and final output. */
            Buffer<std::uint32_t> second; /**< What the rule's first node writes. */
            Buffer<std::uint32_t> third;  /**< What the rule's second node writes. */
            std::size_t grouped = 0;           /**< Position of the three-node rule. */
        };

        /**
         * @brief Records K_RULE_NODES kernels in one rule scope, each reading what the last wrote.
         * @param first,second,third Hold K_WIDTH words each; must outlive the recorder.
         */
        void record_grouped_rule(Recorder& recorder, const Buffer<std::uint32_t>& first,
                                 const Buffer<std::uint32_t>& second,
                                 const Buffer<std::uint32_t>& third)
        {
            std::uint32_t* a = first.data();
            std::uint32_t* b = second.data();
            std::uint32_t* c = third.data();

            auto rule = recorder.rule();
            recorder.kernel(K_WIDTH, Access(Reads(first), Writes(second)),
                            [a, b](std::size_t i) { b[i] = a[i] + 1u; });
            recorder.kernel(K_WIDTH, Access(Reads(second), Writes(third)),
                            [b, c](std::size_t i) { c[i] = b[i] * 2u; });
            recorder.kernel(K_WIDTH, Access(Reads(third), Writes(first)),
                            [c, a](std::size_t i) { a[i] = c[i]; });
        }

        /**
         * @brief Records three plain kernels and one rule of three nodes onto a fresh graph.
         *
         * The rule is the interesting half: its nodes share one registration index, which is what
         * makes the add() order and the report's numbering diverge.
         */
        Recorded record(Runtime& rt)
        {
            Recorded r{rt.graph(),
                       rt.buffer<float>(K_WIDTH).value(),
                       rt.buffer<std::uint32_t>(K_WIDTH).value(),
                       rt.buffer<std::uint32_t>(K_WIDTH).value(),
                       rt.buffer<std::uint32_t>(K_WIDTH).value(),
                       0};
            for (std::size_t i = 0; i < K_WIDTH; ++i)
            {
                r.values[i] = 1.0f;
                r.first[i] = static_cast<std::uint32_t>(i % 7);
                r.second[i] = 0;
                r.third[i] = 0;
            }

            float* v = r.values.data();
            for (std::size_t k = 0; k < K_PLAIN_RULES; ++k)
                r.graph.add(K_WIDTH, Reads(r.values), Writes(r.values),
                            [v](std::size_t i) { v[i] = v[i] * 1.000001f; });

            record_grouped_rule(r.graph.recorder(), r.first, r.second, r.third);
            r.grouped = r.graph.last_rule();
            r.graph.set_profiling(true);
            return r;
        }
    } // namespace

    TEST(Medium_RuleRegistration, ARuleCarriesNoIndexUntilTheGraphCompiles)
    {
        auto rt = Runtime::create().value();
        Recorded r = record(rt);

        ASSERT_EQ(r.graph.size(), K_PLAIN_RULES + 1u);
        ASSERT_EQ(r.graph.node_count(), K_PLAIN_RULES + K_RULE_NODES);
        for (std::size_t rule = 0; rule < r.graph.size(); ++rule)
            EXPECT_EQ(r.graph.rule_registration(rule), Graph::K_NO_REGISTRATION)
                << "rule " << rule << " reported an index before the first run";
    }

    TEST(Medium_RuleRegistration, ARuleScopeTakesOneIndexOfItsOwn)
    {
        auto rt = Runtime::create().value();
        Recorded r = record(rt);
        RunReport report;
        r.graph.run(1, report).value();
        ASSERT_TRUE(report.is_successful);

        std::set<std::uint32_t> plain;
        for (std::size_t rule = 0; rule < K_PLAIN_RULES; ++rule)
        {
            const std::uint32_t index = r.graph.rule_registration(rule);
            EXPECT_NE(index, Graph::K_NO_REGISTRATION);
            plain.insert(index);
        }
        EXPECT_EQ(plain.size(), K_PLAIN_RULES) << "a plain add() must take an index of its own";

        ASSERT_EQ(r.grouped, K_PLAIN_RULES) << "the rule scope is the rule after the plain ones";
        const std::uint32_t shared = r.graph.rule_registration(r.grouped);
        EXPECT_NE(shared, Graph::K_NO_REGISTRATION);
        EXPECT_EQ(plain.count(shared), 0u) << "the rule must not reuse a plain add()'s index";
        EXPECT_EQ(r.graph.rule_registration(r.graph.size()), Graph::K_NO_REGISTRATION)
            << "a position past the last rule names no rule";

        std::size_t reported = 0;
        for (const NodeEventRecord& event : report.node_events)
            reported += event.node_index == shared ? 1u : 0u;
        EXPECT_EQ(reported, K_RULE_NODES) << "every node of the rule scope must report its one index";
    }

    TEST(Medium_RuleRegistration, EveryNodeEventNamesARecordedRule)
    {
        auto rt = Runtime::create().value();
        Recorded r = record(rt);
        RunReport report;
        r.graph.run(1, report).value();
        ASSERT_TRUE(report.is_successful);
        ASSERT_FALSE(report.node_events.empty()) << "profiling was requested, so events must exist";

        std::set<std::uint32_t> recorded;
        for (std::size_t rule = 0; rule < r.graph.size(); ++rule)
            recorded.insert(r.graph.rule_registration(rule));

        for (const NodeEventRecord& event : report.node_events)
            EXPECT_EQ(recorded.count(event.node_index), 1u)
                << "node event " << event.node_index << " matches no rule the caller recorded";
    }

    TEST(Medium_RuleRegistration, ARulesNodesReportOneIndexOnEveryStep)
    {
        constexpr std::size_t STEPS = 4;

        auto rt = Runtime::create().value();
        Recorded r = record(rt);
        RunReport report;
        r.graph.run(STEPS, report).value();
        ASSERT_TRUE(report.is_successful);
        ASSERT_EQ(report.node_events.size(), (K_PLAIN_RULES + K_RULE_NODES) * STEPS);

        const std::uint32_t shared = r.graph.rule_registration(r.grouped);
        std::vector<std::size_t> per_invocation(STEPS, 0);
        for (const NodeEventRecord& event : report.node_events)
        {
            if (event.node_index != shared)
                continue;
            ASSERT_LT(event.invocation, STEPS);
            ++per_invocation[event.invocation];
        }
        for (std::size_t step = 0; step < STEPS; ++step)
            EXPECT_EQ(per_invocation[step], K_RULE_NODES)
                << "step " << step << ": every node of the rule must report the rule's index";
        EXPECT_EQ(r.graph.compile_count(), 1u);
    }

    /** @brief Tests that the position a Recorder verb returns is the one rule_registration() takes. */
    TEST(Medium_RuleRegistration, AVerbReturnsThePositionOfTheRuleItRecorded)
    {
        constexpr std::uint32_t K_SCALE_MARK = 11;
        constexpr std::uint32_t K_COPY_MARK = 22;

        auto rt = Runtime::create().value();
        auto words = rt.buffer<std::uint32_t>(K_WIDTH).value();
        auto copies = rt.buffer<std::uint32_t>(K_WIDTH).value();
        std::uint32_t* w = words.data();
        std::uint32_t* c = copies.data();
        for (std::size_t i = 0; i < K_WIDTH; ++i)
            w[i] = static_cast<std::uint32_t>(i);

        // Each node states a width no other node has, so its event is found by that width.
        NodeOptions scale_options;
        scale_options.width = K_SCALE_MARK;
        NodeOptions copy_options;
        copy_options.width = K_COPY_MARK;

        auto graph = rt.graph();
        graph.set_profiling(true);
        Recorder& recorder = graph.recorder();

        const std::size_t scale = recorder.kernel(K_WIDTH, Access(Reads(words), Writes(words)),
                                                  [w](std::size_t i) { w[i] *= 2u; },
                                                  scale_options);
        EXPECT_EQ(scale, graph.last_rule());

        std::size_t copy = 0;
        std::size_t idle = 0;
        {
            auto rule = recorder.rule();
            copy = recorder.kernel(K_WIDTH, Access(Reads(words), Writes(copies)),
                                   [w, c](std::size_t i) { c[i] = w[i]; }, copy_options);
            idle = recorder.host(Access(Reads(copies), Writes()), [] {});
        }
        EXPECT_EQ(copy, graph.last_rule());
        EXPECT_EQ(idle, copy) << "the nodes of one scope belong to one rule";
        EXPECT_NE(scale, copy);

        const auto report = graph.run().value();

        std::uint32_t scale_index = Graph::K_NO_REGISTRATION;
        std::uint32_t copy_index = Graph::K_NO_REGISTRATION;
        for (const NodeEventRecord& event : report.node_events)
        {
            if (event.launched_width == K_SCALE_MARK)
                scale_index = event.node_index;
            if (event.launched_width == K_COPY_MARK)
                copy_index = event.node_index;
        }

        ASSERT_NE(scale_index, Graph::K_NO_REGISTRATION);
        ASSERT_NE(copy_index, Graph::K_NO_REGISTRATION);
        EXPECT_EQ(graph.rule_registration(scale), scale_index);
        EXPECT_EQ(graph.rule_registration(copy), copy_index);
        EXPECT_EQ(copies[K_WIDTH - 1], static_cast<std::uint32_t>(K_WIDTH - 1) * 2u);
    }
} // namespace RuleRegistrationTests
