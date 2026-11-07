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
 * @file test_recorder_semantics.cpp
 * @brief Tests what a rule, a name, a move, a region access and a liveness observer mean to a Recorder.
 * @see tests/surface/api/graph/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>

#include "recorder_buffers.hpp"
#include "recorder_fixtures.hpp"
#include "run_recorder.hpp"

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

    namespace
    {
        using RecorderSupport::K_WIDTH;
        using RecorderSupport::zeroed;

        constexpr std::size_t K_RULE_NODES = 3;
        constexpr std::uint64_t K_CELL = 7;

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

        /** @brief Counts the node table rows of observation that carry name. */
        std::size_t rows_named(const RunObservation& observation, const std::string& name)
        {
            std::size_t rows = 0;
            if (observation.table == nullptr)
                return rows;
            for (const NodeTableRow& row : observation.table->rows())
                rows += row.name == name ? 1u : 0u;
            return rows;
        }
    } // namespace

    /** @brief Runs the RecorderSemantics tests on a host runtime. */
    class Small_RecorderSemantics : public RecorderHostTest
    {
    };

    /** @brief Runs the RecorderSemantics test whose task body needs a device queue. */
    class Medium_RecorderSemantics : public RecorderTest
    {
    };

    TEST_F(Small_RecorderSemantics, ReadingARegionBetweenRunsDoesNotRecompose)
    {
        const Runtime& rt = runtime();
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

    TEST_F(Small_RecorderSemantics, ARecordingThroughAHeldRegionRecomposes)
    {
        const Runtime& rt = runtime();
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

    TEST_F(Small_RecorderSemantics, ClearingARegionReplacesItsWork)
    {
        const Runtime& rt = runtime();
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

    TEST_F(Small_RecorderSemantics, ADropBetweenRunsRecomposes)
    {
        const Runtime& rt = runtime();
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

    TEST_F(Small_RecorderSemantics, AThreeNodeRuleIsOneRuleWithOneName)
    {
        const Runtime& rt = runtime();
        Testing::RunRecorder observed(rt);
        auto values = zeroed(rt);
        auto graph = rt.graph();
        record_three_node_rule(graph.recorder(), values);
        graph.name("pipeline");

        EXPECT_EQ(graph.size(), 1u);
        EXPECT_EQ(graph.node_count(), K_RULE_NODES);
        EXPECT_EQ(graph.last_rule(), 0u);

        RunReport report;
        graph.run(1, report).value();

        // The nodes of a rule share one registration, and the table holds one row per registration.
        ASSERT_EQ(observed.count(), 1u);
        const RunObservation observation = observed.last();
        ASSERT_NE(observation.table, nullptr);
        ASSERT_EQ(observation.table->size(), 1u);
        EXPECT_EQ(rows_named(observation, "pipeline"), 1u) << "the rule's row must carry the rule's name";
        EXPECT_NE(graph.rule_registration(0), Graph::K_NO_REGISTRATION);
        EXPECT_EQ(graph.rule_registration(1), Graph::K_NO_REGISTRATION)
            << "the graph holds one rule, so position 1 names none";
        EXPECT_EQ(values[0], static_cast<std::uint32_t>(K_RULE_NODES));
    }

    TEST_F(Small_RecorderSemantics, ARegionRuleReportsItsRegistrationAndName)
    {
        const Runtime& rt = runtime();
        Testing::RunRecorder observed(rt);
        auto first = zeroed(rt);
        auto second = zeroed(rt);
        auto world = rt.dynamic_graph();
        add_increment(world.region(K_CELL), first, 1u);
        record_three_node_rule(world.region(K_CELL + 1).recorder(), second);
        world.region(K_CELL + 1).name("pipeline");

        EXPECT_EQ(world.size(), 2u);
        EXPECT_EQ(world.node_count(), 1u + K_RULE_NODES);
        EXPECT_EQ(world.rule_registration(K_CELL + 1, 0), DynamicGraph::K_NO_REGISTRATION);

        RunReport report;
        world.run(1, report).value();

        ASSERT_EQ(observed.count(), 1u);
        EXPECT_EQ(rows_named(observed.last(), "pipeline"), 1u);
        EXPECT_EQ(world.rule_registration(K_CELL, 0), 0u);
        EXPECT_EQ(world.rule_registration(K_CELL + 1, 0), 1u)
            << "the three nodes of the second region's rule share the index after the first";
        EXPECT_EQ(world.rule_registration(K_CELL + 1, 1), DynamicGraph::K_NO_REGISTRATION);
        EXPECT_EQ(world.rule_registration(K_CELL + 2, 0), DynamicGraph::K_NO_REGISTRATION);
    }

    TEST_F(Small_RecorderSemantics, NameLastOwnsItsText)
    {
        const Runtime& rt = runtime();
        Testing::RunRecorder observed(rt);
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

        RunReport report;
        graph.run(1, report).value();

        ASSERT_EQ(observed.count(), 1u);
        const RunObservation observation = observed.last();
        ASSERT_NE(observation.table, nullptr);
        ASSERT_EQ(observation.table->size(), 2u);
        EXPECT_EQ(rows_named(observation, "integrate_velocities_of_the_seventh_cell"), 1u)
            << "the name must survive the string it was read from";
        EXPECT_EQ(rows_named(observation, "integrate_7"), 1u)
            << "a temporary must be a valid argument, and the last name given wins";
    }

    TEST_F(Small_RecorderSemantics, ANullOrEmptyNameLeavesTheDefault)
    {
        const Runtime& rt = runtime();
        Testing::RunRecorder observed(rt);
        auto values = zeroed(rt);
        auto graph = rt.graph();
        add_increment(graph, values, 1u);
        RunReport unnamed;
        graph.run(1, unnamed).value();
        ASSERT_EQ(observed.count(), 1u);
        const RunObservation first = observed.last();
        ASSERT_NE(first.table, nullptr);
        ASSERT_EQ(first.table->size(), 1u);

        const char* absent = nullptr;
        graph.name("named").name(absent);
        RunReport report;
        graph.run(1, report).value();

        ASSERT_EQ(observed.count(), 2u);
        const RunObservation second = observed.last();
        ASSERT_NE(second.table, nullptr);
        ASSERT_EQ(second.table->size(), 1u);
        EXPECT_EQ(second.table->rows()[0].name, first.table->rows()[0].name);
    }

    /** @brief Tests that a moved-from graph is empty and refuses a run. */
    TEST_F(Small_RecorderSemantics, MovedFromGraphIsEmpty)
    {
        const Runtime& rt = runtime();
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
    }

    /** @brief Tests that the compiled plan moves with the graph that took it. */
    TEST_F(Small_RecorderSemantics, ThePlanMovesWithTheTakenGraph)
    {
        const Runtime& rt = runtime();
        auto values = zeroed(rt);
        auto graph = rt.graph();
        add_increment(graph, values, 1u);
        graph.run().value();
        ASSERT_EQ(graph.compile_count(), 1u);

        Graph taken = std::move(graph);

        EXPECT_EQ(taken.size(), 1u);
        taken.run().value();
        EXPECT_EQ(taken.compile_count(), 1u) << "the plan moves with the graph";
        EXPECT_EQ(values[0], 2u);
    }

    /** @brief Tests that a moved-from graph records and runs again. */
    TEST_F(Small_RecorderSemantics, AMovedFromGraphRecordsAndRunsAgain)
    {
        const Runtime& rt = runtime();
        auto values = zeroed(rt);
        auto graph = rt.graph();
        add_increment(graph, values, 1u);
        graph.run().value();

        Graph taken = std::move(graph);

        add_increment(graph, values, 10u);
        graph.run().value();
        EXPECT_EQ(graph.size(), 1u);
        EXPECT_EQ(values[0], 11u) << "a moved-from graph records and runs again";
    }

    /** @brief Tests that a moved-from dynamic graph has no regions and refuses a run. */
    TEST_F(Small_RecorderSemantics, MovedFromDynamicGraphHasNoRegions)
    {
        const Runtime& rt = runtime();
        auto values = zeroed(rt);
        auto world = rt.dynamic_graph();
        add_increment(world.region(K_CELL), values, 1u);
        world.run().value();

        DynamicGraph taken = std::move(world);

        EXPECT_EQ(world.region_count(), 0u);
        EXPECT_EQ(world.size(), 0u);
        EXPECT_EQ(world.compose_count(), 0u);
        EXPECT_FALSE(world.run(std::size_t{1}));
    }

    /** @brief Tests that the composed plan moves with the dynamic graph that took it, and its regions still report to it. */
    TEST_F(Small_RecorderSemantics, TheComposedPlanMovesWithTheTakenDynamicGraph)
    {
        const Runtime& rt = runtime();
        auto values = zeroed(rt);
        auto world = rt.dynamic_graph();
        add_increment(world.region(K_CELL), values, 1u);
        world.run().value();

        DynamicGraph taken = std::move(world);

        EXPECT_EQ(taken.region_count(), 1u);
        taken.run().value();
        EXPECT_EQ(taken.compose_count(), 1u) << "the composed plan moves with the graph";
        EXPECT_EQ(values[0], 2u);

        add_increment(taken.region(K_CELL), values, 10u);
        taken.run().value();
        EXPECT_EQ(taken.compose_count(), 2u) << "a region still reports to the graph that owns it";
        EXPECT_EQ(values[0], 13u);
    }

    /** @brief Tests that a moved-from dynamic graph takes regions and runs again. */
    TEST_F(Small_RecorderSemantics, AMovedFromDynamicGraphTakesRegionsAndRunsAgain)
    {
        const Runtime& rt = runtime();
        auto values = zeroed(rt);
        auto world = rt.dynamic_graph();
        add_increment(world.region(K_CELL), values, 1u);
        world.run().value();

        DynamicGraph taken = std::move(world);

        add_increment(world.region(K_CELL), values, 100u);
        world.run().value();
        EXPECT_EQ(values[0], 101u) << "a moved-from graph takes regions and runs again";
    }

    /** @brief Tests that many nodes over one buffer, and a node over another, all run and add up. */
    TEST_F(Small_RecorderSemantics, ManyNodesOverOneBufferAddUp)
    {
        constexpr std::size_t NODES = 16;

        const Runtime& rt = runtime();
        auto shared = zeroed(rt);
        auto other = zeroed(rt);
        auto graph = rt.graph();
        for (std::size_t node = 0; node < NODES; ++node)
            add_increment(graph, shared, 1u);
        add_increment(graph, other, 1u);
        add_increment(graph, shared, 1u);

        graph.run().value();
        EXPECT_EQ(shared[0], static_cast<std::uint32_t>(NODES + 1));
        EXPECT_EQ(other[0], 1u);
    }

    TEST_F(Small_RecorderSemantics, ADroppedBufferIsStillDetected)
    {
        const Runtime& rt = runtime();
        auto kept = zeroed(rt);
        std::optional<Buffer<std::uint32_t>> gone(zeroed(rt));
        auto graph = rt.graph();
        add_increment(graph, *gone, 1u);
        for (std::size_t node = 0; node < 4; ++node)
            add_increment(graph, kept, 1u);
        add_increment(graph, *gone, 1u);
        ASSERT_TRUE(graph.run());

        gone.reset();

        auto refused = graph.run();
        ASSERT_FALSE(refused);
        EXPECT_EQ(refused.error().code, ErrorCode::DanglingResource);
    }

    TEST_F(Small_RecorderSemantics, ADroppedBufferIsStillDetectedInARegion)
    {
        const Runtime& rt = runtime();
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

    /** @brief Tests that the work a graph moved out keeps the generation it had. */
    TEST_F(Small_RecorderSemantics, TheMovedWorkKeepsItsGeneration)
    {
        const Runtime& rt = runtime();
        auto values = zeroed(rt);
        auto graph = rt.graph();
        add_increment(graph, values, 1u);
        graph.run().value();

        const std::uint64_t before = graph.recorder().generation();
        Graph taken = std::move(graph);

        EXPECT_EQ(taken.recorder().generation(), before)
            << "the work moved keeps the generation it had";
    }

    /** @brief Tests that a generation the plan was built at never names a rule recorded later on the moved-from graph. */
    TEST_F(Small_RecorderSemantics, AGenerationThePlanWasBuiltAtNeverNamesANewRule)
    {
        const Runtime& rt = runtime();
        auto values = zeroed(rt);
        auto graph = rt.graph();
        add_increment(graph, values, 1u);
        graph.run().value();
        ASSERT_EQ(values[0], 1u);

        const Recorder& source = graph.recorder();
        const std::uint64_t built_at = source.generation();
        Graph taken = std::move(graph);
        EXPECT_NE(source.generation(), built_at);

        add_increment(graph, values, 10u);
        EXPECT_NE(source.generation(), built_at)
            << "a generation the plan was built at must not name the new rule";
        graph.run().value();
        EXPECT_EQ(values[0], 11u) << "the plan of the rule moved out must not run again";
    }

    /** @brief Tests that a move assignment does not step the generation of the target back. */
    TEST_F(Small_RecorderSemantics, AMoveAssignmentDoesNotStepTheGenerationBack)
    {
        const Runtime& rt = runtime();
        auto values = zeroed(rt);
        auto graph = rt.graph();
        add_increment(graph, values, 1u);
        Graph taken = std::move(graph);

        auto other = rt.graph();
        add_increment(other, values, 100u);
        const std::uint64_t before = taken.recorder().generation();
        ASSERT_LE(other.recorder().generation(), before);
        taken = std::move(other);
        EXPECT_GT(taken.recorder().generation(), before) << "a move assignment must not step back";
        EXPECT_EQ(taken.node_count(), 1u);
    }

    /** @brief Tests that Graph::clear() forgets the rules and their registrations. */
    TEST_F(Small_RecorderSemantics, GraphClearForgetsTheRulesAndTheirRegistration)
    {
        const Runtime& rt = runtime();
        auto values = zeroed(rt);
        auto graph = rt.graph();
        add_increment(graph, values, 1u);
        graph.run().value();
        ASSERT_NE(graph.rule_registration(0), Graph::K_NO_REGISTRATION);

        graph.clear();

        EXPECT_EQ(graph.size(), 0u);
        EXPECT_EQ(graph.rule_registration(0), Graph::K_NO_REGISTRATION);
    }

    /** @brief Tests that a cleared graph refuses an empty run. */
    TEST_F(Small_RecorderSemantics, AClearedGraphRefusesAnEmptyRun)
    {
        const Runtime& rt = runtime();
        auto values = zeroed(rt);
        auto graph = rt.graph();
        add_increment(graph, values, 1u);
        graph.run().value();

        graph.clear();

        const auto empty_run = graph.run();
        ASSERT_FALSE(empty_run);
        EXPECT_EQ(empty_run.error().code, ErrorCode::InvalidGraph);
    }

    /** @brief Tests that the rule a clear forgot does not run again and a new rule does. */
    TEST_F(Small_RecorderSemantics, AClearedRuleDoesNotRunAgainAndANewOneDoes)
    {
        const Runtime& rt = runtime();
        auto values = zeroed(rt);
        auto graph = rt.graph();
        add_increment(graph, values, 1u);
        graph.run().value();
        ASSERT_EQ(values[0], 1u);

        graph.clear();
        add_increment(graph, values, 10u);
        graph.run().value();
        EXPECT_EQ(values[0], 11u) << "the plan of the cleared rule must not run again";
    }

    /** @brief Tests that Graph::clear() frees what the task bodies of its compiled plan hold. */
    TEST_F(Small_RecorderSemantics, Regression_GraphClearFreesWhatItsCompiledPlanCaptured)
    {
        const Runtime& rt = runtime();
        auto graph = rt.graph();
        std::weak_ptr<std::vector<int>> watched;
        {
            auto operand = std::make_shared<std::vector<int>>(64, 0);
            watched = operand;
            graph.recorder().host(Access(), [operand] { ++(*operand)[0]; });
        }
        graph.run().value();
        ASSERT_FALSE(watched.expired()) << "the plan holds the body until the clear";

        graph.clear();
        EXPECT_TRUE(watched.expired()) << "clear() must free the compiled plan and its captures";
        EXPECT_FALSE(graph.run()) << "a cleared graph has nothing to run";
    }

    /** @brief Tests that clear() inside an extension leaves a recorder that records one rule. */
    TEST_F(Small_RecorderSemantics, AClearInsideAnExtensionStartsANewRule)
    {
        const Runtime& rt = runtime();
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
    TEST_F(Medium_RecorderSemantics, ATaskOfTwoCommandGroupsIsOrderedByItsAccess)
    {
        const Runtime& rt = runtime();
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


    TEST_F(Small_RecorderSemantics, AnExtensionThatRecordsNothingAddsNoRule)
    {
        const Runtime& rt = runtime();
        Testing::RunRecorder observed(rt);
        auto values = zeroed(rt);
        auto graph = rt.graph();
        add_increment(graph, values, 1u);
        graph.name("integrate");
        graph.extend([](Recorder&) {});

        RunReport report;
        graph.run(1, report).value();

        EXPECT_EQ(graph.size(), 1u);
        EXPECT_EQ(graph.node_count(), 1u);
        ASSERT_EQ(observed.count(), 1u);
        EXPECT_EQ(rows_named(observed.last(), "integrate"), 1u) << "the first rule must keep its name";
        EXPECT_NE(graph.rule_registration(0), Graph::K_NO_REGISTRATION);
        EXPECT_EQ(values[0], 1u);
    }
} // namespace RecorderSemanticsTests
