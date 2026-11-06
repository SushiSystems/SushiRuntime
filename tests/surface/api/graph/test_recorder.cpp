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
 * @file test_recorder.cpp
 * @brief Tests the Recorder: its semantics, its scratch memory and the indices of the rules it records.
 * @see tests/surface/api/graph/README.md
 * @author Mustafa Garip
 */

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>

#include "run_recorder.hpp"
#include "support/runtime_environment.hpp"

/** @brief Fixture base of the recorder tests: the program's runtime, with the placement rules cleared after each test. */
class RecorderTest : public TestSupport::RuntimeTest
{
    protected:
        /** @brief Clears the rules, so no later test places a node by this test's rule. */
        void TearDown() override { (void)runtime().clear_placement_rules(); }
};

/** @brief The entry points that take the parameters of a named rule, as the rows of one test. */
enum class ParameterEntry
{
    Add,     /**< Graph::add(). */
    AddRule  /**< Graph::add_rule(). */
};

/** @brief Fixture of the rows over the entry points that take parameters. */
class Medium_RecorderParameterLimit : public RecorderTest,
                                      public ::testing::WithParamInterface<ParameterEntry>
{
};

/** @brief The untracked verbs a Fail rule refuses, as the rows of one test. */
enum class Verb
{
    CountKernel,  /**< untracked_kernel() over a count. */
    RawKernel     /**< untracked_raw_kernel(). */
};

/** @brief Fixture of the rows over the untracked verbs. */
class Medium_RecorderPlacementScopeVerb : public RecorderTest,
                                          public ::testing::WithParamInterface<Verb>
{
};

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
        constexpr std::size_t K_WIDTH = 32;
        constexpr std::size_t K_RULE_NODES = 3;
        constexpr std::uint64_t K_CELL = 7;

        /** @brief Allocates K_WIDTH words and zeroes them. */
        Buffer<std::uint32_t> zeroed(const Runtime& rt)
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

    /** @brief Fixture of the RecorderSemantics tests, which run on the program's runtime. */
    class Medium_RecorderSemantics : public RecorderTest
    {
    };

    TEST_F(Medium_RecorderSemantics, ReadingARegionBetweenRunsDoesNotRecompose)
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

    TEST_F(Medium_RecorderSemantics, ARecordingThroughAHeldRegionRecomposes)
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

    TEST_F(Medium_RecorderSemantics, ClearingARegionReplacesItsWork)
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

    TEST_F(Medium_RecorderSemantics, ADropBetweenRunsRecomposes)
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

    TEST_F(Medium_RecorderSemantics, AThreeNodeRuleIsOneRuleWithOneName)
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

    TEST_F(Medium_RecorderSemantics, ARegionRuleReportsItsRegistrationAndName)
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

    TEST_F(Medium_RecorderSemantics, NameLastOwnsItsText)
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

    TEST_F(Medium_RecorderSemantics, ANullOrEmptyNameLeavesTheDefault)
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
    TEST_F(Medium_RecorderSemantics, MovedFromGraphIsEmpty)
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
    TEST_F(Medium_RecorderSemantics, ThePlanMovesWithTheTakenGraph)
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
    TEST_F(Medium_RecorderSemantics, AMovedFromGraphRecordsAndRunsAgain)
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
    TEST_F(Medium_RecorderSemantics, MovedFromDynamicGraphHasNoRegions)
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
    TEST_F(Medium_RecorderSemantics, TheComposedPlanMovesWithTheTakenDynamicGraph)
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
    TEST_F(Medium_RecorderSemantics, AMovedFromDynamicGraphTakesRegionsAndRunsAgain)
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
    TEST_F(Medium_RecorderSemantics, ManyNodesOverOneBufferAddUp)
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

    TEST_F(Medium_RecorderSemantics, ADroppedBufferIsStillDetected)
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

    TEST_F(Medium_RecorderSemantics, ADroppedBufferIsStillDetectedInARegion)
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
    TEST_F(Medium_RecorderSemantics, TheMovedWorkKeepsItsGeneration)
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
    TEST_F(Medium_RecorderSemantics, AGenerationThePlanWasBuiltAtNeverNamesANewRule)
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
    TEST_F(Medium_RecorderSemantics, AMoveAssignmentDoesNotStepTheGenerationBack)
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
    TEST_F(Medium_RecorderSemantics, GraphClearForgetsTheRulesAndTheirRegistration)
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
    TEST_F(Medium_RecorderSemantics, AClearedGraphRefusesAnEmptyRun)
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
    TEST_F(Medium_RecorderSemantics, AClearedRuleDoesNotRunAgainAndANewOneDoes)
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
    TEST_F(Medium_RecorderSemantics, Regression_GraphClearFreesWhatItsCompiledPlanCaptured)
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
    TEST_F(Medium_RecorderSemantics, AClearInsideAnExtensionStartsANewRule)
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

    /** @brief Tests that a rule that names an operation is refused nine parameters and records nothing. */
    TEST_P(Medium_RecorderParameterLimit, NineParametersAreRefusedAndRecordNothing)
    {
        const Runtime& rt = runtime();
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
        NodeOptions options;
        options.operation = "recorder.limit"_op;
        options.parameters = nine;

        const auto refusal = refusal_of(graph.recorder(), [&]
        {
            if (GetParam() == ParameterEntry::Add)
                graph.add(K_WIDTH, Reads(values), Writes(values), local, options);
            else
                graph.add_rule(field, rule, options);
        });

        ASSERT_TRUE(refusal);
        EXPECT_EQ(refusal->code, ErrorCode::InvalidGraph);
        EXPECT_NE(refusal->message.find("9 parameters"), std::string::npos) << refusal->message;
        EXPECT_NE(refusal->message.find("at most 8"), std::string::npos) << refusal->message;
        EXPECT_EQ(graph.size(), 0u) << "a refused rule must not be recorded";
    }

    /** @brief Tests that a rule that names an operation keeps eight parameters. */
    TEST_P(Medium_RecorderParameterLimit, EightParametersAreKept)
    {
        const Runtime& rt = runtime();
        auto values = zeroed(rt);
        auto field = rt.state<std::uint32_t>(K_WIDTH).value();
        std::uint32_t* words = values.data();
        const auto local = [words](std::size_t i) { words[i] += 1u; };
        const auto rule = [](std::size_t i, const std::uint32_t* current, std::uint32_t* next)
        {
            next[i] = current[i];
        };
        auto graph = rt.graph();

        const std::uint64_t eight[] = {1, 2, 3, 4, 5, 6, 7, 8};
        NodeOptions options;
        options.operation = "recorder.limit"_op;
        options.parameters = eight;

        if (GetParam() == ParameterEntry::Add)
            graph.add(K_WIDTH, Reads(values), Writes(values), local, options);
        else
            graph.add_rule(field, rule, options);

        EXPECT_EQ(graph.size(), 1u);
    }

    INSTANTIATE_TEST_SUITE_P(Rows, Medium_RecorderParameterLimit,
                             ::testing::Values(ParameterEntry::Add, ParameterEntry::AddRule));

    TEST_F(Medium_RecorderSemantics, AnExtensionThatRecordsNothingAddsNoRule)
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

    /** @brief Fixture of the RecorderScratch tests, which run on the program's runtime. */
    class Medium_RecorderScratch : public RecorderTest
    {
    };

    /** @brief Tests that a consumer of a rule's output sees what passed through its scratch. */
    TEST_F(Medium_RecorderScratch, AConsumerSeesWhatPassedThroughTheScratch)
    {
        const Runtime& rt = runtime();
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

    /** @brief Tests that each run of a replayed rule reads its input as it stands. */
    TEST_F(Medium_RecorderScratch, EachRunReadsItsInputAsItStands)
    {
        const Runtime& rt = runtime();
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
    }

    /** @brief Tests that a replayed rule compiles once. */
    TEST_F(Medium_RecorderScratch, AReplayedRuleCompilesOnce)
    {
        const Runtime& rt = runtime();
        auto in  = rt.buffer<std::uint32_t>(K_COUNT).value();
        auto out = rt.buffer<std::uint32_t>(K_COUNT).value();

        auto g = rt.graph();
        record_staged_rule(g.recorder(), in, out);

        for (std::uint32_t step = 0; step < 4; ++step)
            g.run().value();

        EXPECT_EQ(g.compile_count(), 1u);
    }

    TEST_F(Medium_RecorderScratch, ScratchSurvivesARecompile)
    {
        const Runtime& rt = runtime();
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

        for (std::size_t i = 0; i < K_COUNT; ++i)
        {
            ASSERT_EQ(out[i], staged(in[i])) << "second plan, i = " << i;
            ASSERT_EQ(copy[i], staged(in[i])) << "second plan, i = " << i;
        }
    }


    /** @brief Tests that regions record rules with scratch and pass values through it across rounds. */
    TEST_F(Medium_RecorderScratch, RegionsRecordRulesWithScratch)
    {
        const Runtime& rt = runtime();
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
    }

    /** @brief Tests that renaming a region's rule rebuilds that region once. */
    TEST_F(Medium_RecorderScratch, RenamingARegionsRuleRebuildsThatRegionOnce)
    {
        const Runtime& rt = runtime();
        auto in  = rt.buffer<std::uint32_t>(K_COUNT).value();
        auto mid = rt.buffer<std::uint32_t>(K_COUNT).value();
        auto out = rt.buffer<std::uint32_t>(K_COUNT).value();

        auto world = rt.dynamic_graph();
        record_staged_rule(world.region(1).recorder(), in, mid);
        record_staged_rule(world.region(2).recorder(), mid, out);
        world.run().value();
        ASSERT_EQ(world.compose_count(), 1u);

        world.region(1).name("staged");
        world.run().value();

        EXPECT_EQ(world.region(1).node_count(), 2u);
        EXPECT_EQ(world.compose_count(), 2u) << "the rename must have rebuilt region 1 once";
    }

    /** @brief Tests that scratch no device can hold comes back as AllocationFailed. */
    TEST_F(Medium_RecorderScratch, AScratchRequestNoDeviceCanSatisfyReturnsAllocationFailed)
    {
        auto graph = runtime().graph();
        // Half the address space in bytes: the count fits std::size_t and no device has the room.
        const std::size_t count = std::numeric_limits<std::size_t>::max() / 2;

        const auto refused =
            graph.recorder().scratch<std::uint8_t>(count, DeviceIndex{0}, Residency::Shared);
        ASSERT_FALSE(refused.has_value());
        EXPECT_EQ(refused.error().code, ErrorCode::AllocationFailed);
    }

    /** @brief Tests that a small scratch request is granted on a recorder that refused a large one. */
    TEST_F(Medium_RecorderScratch, ASmallScratchRequestIsGrantedAfterALargeOneFailed)
    {
        auto graph = runtime().graph();
        const std::size_t count = std::numeric_limits<std::size_t>::max() / 2;
        (void)graph.recorder().scratch<std::uint8_t>(count, DeviceIndex{0}, Residency::Shared);

        const auto granted =
            graph.recorder().scratch<std::uint8_t>(16, DeviceIndex{0}, Residency::Shared);
        EXPECT_TRUE(granted.has_value());
    }
} // namespace RecorderScratchTests

/** @brief Tests that the nodes of one rule scope share one registration index in the node table of a run. */
namespace RegistrationIndexTests
{
    namespace
    {
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
        Stages make_stages(const Runtime& rt)
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

        /** @brief The kinds of node a rule scope groups, as the rows of one test. */
        enum class RuleKind
        {
            Plain,     /**< Three plain kernels. */
            LateBound  /**< A resolver, a device-sized kernel and a plain kernel. */
        };

        /** @brief Records a rule of the given kind. */
        void record_rule(Recorder& recorder, const Stages& stages, RuleKind kind)
        {
            if (kind == RuleKind::Plain)
            {
                record_grouped_rule(recorder, stages);
                return;
            }
            const LateCount count = LateCount::from_host(K_WIDTH, []() { return K_WIDTH; });
            record_late_bound_rule(recorder, stages, count);
        }
    } // namespace

    /** @brief Fixture of the rows that group one kind of rule. */
    class Medium_RegistrationIndex : public RecorderTest, public ::testing::WithParamInterface<RuleKind>
    {
    };

    /** @brief Fixture of the registration-index tests of the plain rule. */
    class Medium_RegistrationIndexRebuild : public RecorderTest
    {
    };

    /** @brief Tests that a rule scope groups every node recorded in it under one index, between two single-node rules. */
    TEST_P(Medium_RegistrationIndex, RuleScopeGroupsItsNodesUnderOneIndex)
    {
        const Runtime& rt = runtime();
        Stages stages = make_stages(rt);
        Testing::RunRecorder observed(rt);

        auto g = rt.graph();
        g.add_untracked(std::size_t{1}, [](std::size_t) {}).name("before");
        record_rule(g.recorder(), stages, GetParam());
        g.add_untracked(std::size_t{1}, [](std::size_t) {}).name("after");
        g.run().value();

        ASSERT_EQ(observed.count(), 1u);
        const RunObservation observation = observed.last();
        ASSERT_NE(observation.table, nullptr);
        ASSERT_EQ(observation.table->size(), 3u) << "the three nodes of the rule take one row";

        const NodeTableRow* before = observation.table->find(0u);
        const NodeTableRow* rule = observation.table->find(1u);
        const NodeTableRow* after = observation.table->find(2u);
        ASSERT_NE(before, nullptr);
        ASSERT_NE(rule, nullptr);
        ASSERT_NE(after, nullptr);
        EXPECT_EQ(before->name, "before");
        EXPECT_EQ(after->name, "after") << "'after' must be before+2: the group draws exactly one index.";
        EXPECT_EQ(rule->multiplicity, K_RULE_NODES);
    }

    INSTANTIATE_TEST_SUITE_P(Rows, Medium_RegistrationIndex,
                             ::testing::Values(RuleKind::Plain, RuleKind::LateBound));

    /** @brief Tests that a rule scope's group survives a rebuild and the indices stay dense. */
    TEST_F(Medium_RegistrationIndexRebuild, RuleScopeGroupSurvivesRebuild)
    {
        const Runtime& rt = runtime();
        Stages stages = make_stages(rt);
        Testing::RunRecorder observed(rt);

        auto g = rt.graph();
        g.add_untracked(std::size_t{1}, [](std::size_t) {}).name("before");
        record_grouped_rule(g.recorder(), stages);
        g.run().value();

        // An add() after the first run dirties the graph, so every spec is replayed; see README.md.
        g.add_untracked(std::size_t{1}, [](std::size_t) {}).name("after");
        g.run().value();

        ASSERT_EQ(observed.count(), 2u);
        const RunObservation observation = observed.last();
        ASSERT_NE(observation.table, nullptr);
        ASSERT_EQ(observation.table->size(), 3u);
        const auto& rows = observation.table->rows();
        for (std::size_t i = 0; i < rows.size(); ++i)
            EXPECT_EQ(rows[i].index, i) << "registration indices must be dense 0..n-1 after rebuild.";
        EXPECT_EQ(rows[1].multiplicity, K_RULE_NODES);
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
        Recorded record(const Runtime& rt)
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
            return r;
        }
    } // namespace

    /** @brief Fixture of the RuleRegistration tests, which run on the program's runtime. */
    class Medium_RuleRegistration : public RecorderTest
    {
    };

    TEST_F(Medium_RuleRegistration, ARuleCarriesNoIndexUntilTheGraphCompiles)
    {
        const Runtime& rt = runtime();
        Recorded r = record(rt);

        ASSERT_EQ(r.graph.size(), K_PLAIN_RULES + 1u);
        ASSERT_EQ(r.graph.node_count(), K_PLAIN_RULES + K_RULE_NODES);
        for (std::size_t rule = 0; rule < r.graph.size(); ++rule)
            EXPECT_EQ(r.graph.rule_registration(rule), Graph::K_NO_REGISTRATION)
                << "rule " << rule << " reported an index before the first run";
    }

    TEST_F(Medium_RuleRegistration, ARuleScopeTakesOneIndexOfItsOwn)
    {
        const Runtime& rt = runtime();
        Testing::RunRecorder observed(rt);
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
        ASSERT_EQ(observed.count(), 1u);
        for (const NodeEventRecord& event : observed.last().events)
            reported += event.node_index == shared ? 1u : 0u;
        EXPECT_EQ(reported, K_RULE_NODES) << "every node of the rule scope must report its one index";
    }

    TEST_F(Medium_RuleRegistration, EveryNodeEventNamesARecordedRule)
    {
        const Runtime& rt = runtime();
        Testing::RunRecorder observed(rt);
        Recorded r = record(rt);
        RunReport report;
        r.graph.run(1, report).value();
        ASSERT_TRUE(report.is_successful);
        ASSERT_EQ(observed.count(), 1u);
        const RunObservation observation = observed.last();
        ASSERT_FALSE(observation.events.empty()) << "an observer is added, so events must exist";

        std::set<std::uint32_t> recorded;
        for (std::size_t rule = 0; rule < r.graph.size(); ++rule)
            recorded.insert(r.graph.rule_registration(rule));

        for (const NodeEventRecord& event : observation.events)
            EXPECT_EQ(recorded.count(event.node_index), 1u)
                << "node event " << event.node_index << " matches no rule the caller recorded";
    }

    TEST_F(Medium_RuleRegistration, ARulesNodesReportOneIndexOnEveryStep)
    {
        constexpr std::size_t STEPS = 4;

        const Runtime& rt = runtime();
        Testing::RunRecorder observed(rt);
        Recorded r = record(rt);
        RunReport report;
        r.graph.run(STEPS, report).value();
        ASSERT_TRUE(report.is_successful);
        ASSERT_EQ(observed.count(), 1u);
        const RunObservation observation = observed.last();
        ASSERT_EQ(observation.events.size(), (K_PLAIN_RULES + K_RULE_NODES) * STEPS);

        const std::uint32_t shared = r.graph.rule_registration(r.grouped);
        std::vector<std::size_t> per_invocation(STEPS, 0);
        for (const NodeEventRecord& event : observation.events)
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
    TEST_F(Medium_RuleRegistration, AVerbReturnsThePositionOfTheRuleItRecorded)
    {
        constexpr std::uint32_t K_SCALE_MARK = 11;
        constexpr std::uint32_t K_COPY_MARK = 22;

        const Runtime& rt = runtime();
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

        Testing::RunRecorder observed(rt);
        auto graph = rt.graph();
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

        graph.run().value();
        ASSERT_EQ(observed.count(), 1u);
        const RunObservation observation = observed.last();

        std::uint32_t scale_index = Graph::K_NO_REGISTRATION;
        std::uint32_t copy_index = Graph::K_NO_REGISTRATION;
        for (const NodeEventRecord& event : observation.events)
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

namespace PlacementScopeRecordingTests
{
    namespace
    {
        /** @brief Records one host node that reads and writes nothing. */
        void record_idle(Recorder& recorder, const NodeOptions& options = {})
        {
            recorder.host(Access(Reads(), Writes()), [] {}, options);
        }
    } // namespace

    /** @brief Records the seven nodes the scope tests read: before every scope, in the outer, in the inner (twice), in the outer again (twice), after every scope. */
    void record_nested_scopes(const Runtime& rt, Graph& graph)
    {
        Recorder& recorder = graph.recorder();
        const PlacementScope outer = rt.placement_scope("outer.group", 7u);
        const PlacementScope inner = rt.placement_scope("inner.group", 0u);

        NodeOptions own_class_and_key;
        own_class_and_key.placement_class = "own.group"_class;
        own_class_and_key.placement_key = 3u;
        NodeOptions own_class_without_key;
        own_class_without_key.placement_class = "own.group"_class;

        record_idle(recorder);
        recorder.within(outer, [&](Recorder& in_outer)
        {
            record_idle(in_outer);
            in_outer.within(inner, [&](Recorder& in_inner)
            {
                record_idle(in_inner);
                record_idle(in_inner, own_class_and_key);
            });
            record_idle(in_outer);
            record_idle(in_outer, own_class_without_key);
        });
        record_idle(recorder);
    }

    /** @brief Fixture of the RecorderPlacementScope tests, which run on the program's runtime. */
    class Medium_RecorderPlacementScope : public RecorderTest
    {
    };

    /** @brief Tests that nodes take the innermost open scope, and none outside every scope. */
    TEST_F(Medium_RecorderPlacementScope, NestedScopesStampTheInnermostClassAndKey)
    {
        auto graph = runtime().graph();
        record_nested_scopes(runtime(), graph);

        const auto plan = graph.plan();
        ASSERT_TRUE(plan);
        ASSERT_EQ(plan->nodes.size(), 7u);

        EXPECT_FALSE(plan->nodes[0].placement_class.is_named()) << "before every scope";
        EXPECT_FALSE(plan->nodes[0].placement_key.has_value());

        EXPECT_EQ(plan->nodes[1].placement_class, "outer.group"_class);
        ASSERT_TRUE(plan->nodes[1].placement_key.has_value());
        EXPECT_EQ(*plan->nodes[1].placement_key, 7u);

        EXPECT_EQ(plan->nodes[2].placement_class, "inner.group"_class);
        ASSERT_TRUE(plan->nodes[2].placement_key.has_value());
        EXPECT_EQ(*plan->nodes[2].placement_key, 0u) << "a key of 0 is a key";

        EXPECT_FALSE(plan->nodes[6].placement_class.is_named()) << "after every scope";
        EXPECT_FALSE(plan->nodes[6].placement_key.has_value());
    }

    /** @brief Tests that a node's own class and key beat the scope's. */
    TEST_F(Medium_RecorderPlacementScope, ANodesOwnClassAndKeyBeatTheScopes)
    {
        auto graph = runtime().graph();
        record_nested_scopes(runtime(), graph);

        const auto plan = graph.plan();
        ASSERT_TRUE(plan);
        ASSERT_EQ(plan->nodes.size(), 7u);

        EXPECT_EQ(plan->nodes[3].placement_class, "own.group"_class);
        ASSERT_TRUE(plan->nodes[3].placement_key.has_value());
        EXPECT_EQ(*plan->nodes[3].placement_key, 3u) << "the own key beats the scope key";

        EXPECT_EQ(plan->nodes[5].placement_class, "own.group"_class);
        EXPECT_FALSE(plan->nodes[5].placement_key.has_value())
            << "a node that names a class keeps a none key; the scope key is not borrowed";
    }

    /** @brief Tests that closing an inner scope restores the outer one. */
    TEST_F(Medium_RecorderPlacementScope, ClosingAScopeRestoresTheOuterOne)
    {
        auto graph = runtime().graph();
        record_nested_scopes(runtime(), graph);

        const auto plan = graph.plan();
        ASSERT_TRUE(plan);
        ASSERT_EQ(plan->nodes.size(), 7u);

        EXPECT_EQ(plan->nodes[4].placement_class, "outer.group"_class)
            << "after the inner scope closed";
        ASSERT_TRUE(plan->nodes[4].placement_key.has_value());
        EXPECT_EQ(*plan->nodes[4].placement_key, 7u);
    }

    /** @brief Tests that within() returns the callable result. */
    TEST_F(Medium_RecorderPlacementScope, WithinReturnsWhatTheCallableReturns)
    {
        const Runtime& rt = runtime();
        auto graph = rt.graph();
        Recorder& recorder = graph.recorder();

        const std::size_t rule = recorder.within(
            rt.placement_scope("a.b"),
            [](Recorder& in_scope)
            {
                return in_scope.host(Access(Reads(), Writes()), [] {});
            });

        EXPECT_EQ(rule, graph.last_rule());
    }

    /** @brief Tests that a callable that throws leaves no scope open on the recorder. */
    TEST_F(Medium_RecorderPlacementScope, AThrowingCallableClosesItsScope)
    {
        const Runtime& rt = runtime();
        auto graph = rt.graph();
        Recorder& recorder = graph.recorder();

        EXPECT_THROW(recorder.within(rt.placement_scope("a.b", 1u),
                                     [](Recorder&) { throw std::runtime_error("unwound"); }),
                     std::runtime_error);
        record_idle(recorder);

        const auto plan = graph.plan();
        ASSERT_TRUE(plan);
        ASSERT_EQ(plan->nodes.size(), 1u);
        EXPECT_FALSE(plan->nodes[0].placement_class.is_named());
        EXPECT_FALSE(plan->nodes[0].placement_key.has_value());
    }
} // namespace PlacementScopeRecordingTests

namespace PlacementScopeDeviceRecordingTests
{
    namespace
    {
        /** @brief Holds the class every rule of this file matches. */
        constexpr const char* K_CLASS = "recorder.device";

        /** @brief Holds the element count of every launch of this file. */
        constexpr std::size_t K_COUNT = 64;

        /** @brief Returns a rule set whose one rule sends K_CLASS to a device index. */
        PlacementRules send_to(std::size_t device, AbsentAction if_absent)
        {
            PlacementRule rule;
            rule.name = "send";
            rule.match.placement_class = K_CLASS;
            rule.target.device_index = device;
            rule.if_absent = AbsentPolicy{if_absent, std::chrono::milliseconds{0}};
            PlacementRules rules;
            EXPECT_TRUE(rules.add(std::move(rule)));
            return rules;
        }
    } // namespace

    /** @brief Fixture of the RecorderPlacementScopeDevice tests, which run on the program's runtime. */
    class Medium_RecorderPlacementScopeDevice : public RecorderTest
    {
    };

    /** @brief Tests that On(scope) runs an untracked verb on device 0 when Local resolves it. */
    TEST_F(Medium_RecorderPlacementScopeDevice, OnAScopeRunsAnUntrackedVerbOnTheLocalDevice)
    {
        const Runtime& rt = runtime();
        ASSERT_TRUE(rt.set_placement_rules(
            send_to(rt.advanced().device_count(), AbsentAction::Local)));
        auto graph = rt.graph();
        Recorder& recorder = graph.recorder();
        auto data = rt.buffer<std::uint32_t>(K_COUNT).value();
        std::uint32_t* words = data.data();

        const std::size_t rule = recorder.untracked_kernel(
            K_COUNT, [words](std::size_t i) { words[i] = static_cast<std::uint32_t>(i) + 1u; },
            On(rt.placement_scope(K_CLASS)));

        EXPECT_EQ(recorder.refusal(), nullptr);
        EXPECT_EQ(rule, 0u);
        ASSERT_TRUE(graph.run());
        for (std::size_t i = 0; i < K_COUNT; ++i)
            ASSERT_EQ(words[i], i + 1u) << "element " << i;
    }

    /** @brief Tests that a scope with no matching rule records the verb without a refusal. */
    TEST_F(Medium_RecorderPlacementScopeDevice, OnAScopeNoRuleMatchesRecordsTheVerb)
    {
        auto graph = runtime().graph();
        Recorder& recorder = graph.recorder();

        recorder.untracked_raw_kernel([](sycl::handler&) {}, On(runtime().placement_scope(K_CLASS)));

        EXPECT_EQ(recorder.refusal(), nullptr);
        EXPECT_EQ(recorder.rule_count(), 1u);
    }

    /** @brief Tests that a scope with no matching rule places its buffers on device 0. */
    TEST_F(Medium_RecorderPlacementScopeDevice, AScopeNoRuleMatchesPlacesItsBufferOnDeviceZero)
    {
        const PlacementScope scope = runtime().placement_scope(K_CLASS);

        auto data = scope.buffer<std::uint32_t>(K_COUNT).value();

        EXPECT_EQ(data.device_index().value(), 0u);
    }

    /** @brief Tests that a Fail rule leaves a refusal naming the rule and the device, and records no node. */
    TEST_P(Medium_RecorderPlacementScopeVerb, OnAScopeThatFailsRefusesTheVerb)
    {
        const Runtime& rt = runtime();
        const std::size_t absent = rt.advanced().device_count();
        ASSERT_TRUE(rt.set_placement_rules(send_to(absent, AbsentAction::Fail)));
        auto graph = rt.graph();
        Recorder& recorder = graph.recorder();
        const PlacementScope scope = rt.placement_scope(K_CLASS);

        if (GetParam() == Verb::CountKernel)
            recorder.untracked_kernel(K_COUNT, [](std::size_t) {}, On(scope));
        else
            recorder.untracked_raw_kernel([](sycl::handler&) {}, On(scope));

        ASSERT_NE(recorder.refusal(), nullptr);
        EXPECT_EQ(recorder.refusal()->code, ErrorCode::InvalidArgument);
        const std::string& message = recorder.refusal()->message;
        EXPECT_NE(message.find("\"send\""), std::string::npos) << message;
        EXPECT_NE(message.find("device index " + std::to_string(absent)), std::string::npos)
            << message;
        EXPECT_EQ(recorder.rule_count(), 0u) << "a refused verb records no node";
    }

    INSTANTIATE_TEST_SUITE_P(Rows, Medium_RecorderPlacementScopeVerb,
                             ::testing::Values(Verb::CountKernel, Verb::RawKernel));

    /** @brief Tests that a kept refusal ends the run with its error. */
    TEST_F(Medium_RecorderPlacementScopeDevice, ARefusedScopeEndsTheRun)
    {
        const Runtime& rt = runtime();
        ASSERT_TRUE(rt.set_placement_rules(
            send_to(rt.advanced().device_count(), AbsentAction::Fail)));
        auto graph = rt.graph();

        graph.recorder().untracked_kernel(K_COUNT, [](std::size_t) {},
                                          On(rt.placement_scope(K_CLASS)));

        const auto run = graph.run();
        ASSERT_FALSE(run);
        EXPECT_EQ(run.error().code, ErrorCode::InvalidArgument);
    }

    /** @brief Tests that a scope opened before a rule set is read against the new set at record time. */
    TEST_F(Medium_RecorderPlacementScopeDevice, AScopeOpenedBeforeTheRulesFollowsTheNewSet)
    {
        const Runtime& rt = runtime();
        auto graph = rt.graph();
        Recorder& recorder = graph.recorder();
        const PlacementScope scope = rt.placement_scope(K_CLASS);
        ASSERT_TRUE(rt.set_placement_rules(
            send_to(rt.advanced().device_count(), AbsentAction::Fail)));

        recorder.untracked_kernel(K_COUNT, [](std::size_t) {}, On(scope));
        EXPECT_NE(recorder.refusal(), nullptr) << "the new set refuses";
    }

    /** @brief Tests that a cleared rule set leaves nothing to refuse. */
    TEST_F(Medium_RecorderPlacementScopeDevice, AClearedSetLeavesNothingToRefuse)
    {
        const Runtime& rt = runtime();
        auto graph = rt.graph();
        Recorder& recorder = graph.recorder();
        const PlacementScope scope = rt.placement_scope(K_CLASS);
        ASSERT_TRUE(rt.set_placement_rules(
            send_to(rt.advanced().device_count(), AbsentAction::Fail)));
        ASSERT_TRUE(rt.clear_placement_rules());

        recorder.untracked_kernel(K_COUNT, [](std::size_t) {}, On(scope));
        EXPECT_EQ(recorder.refusal(), nullptr) << "the cleared set leaves nothing to refuse";
    }

    /** @brief Tests that a scope's buffer lands on device 1 and the tracked node that writes it runs there. */
    TEST_F(Medium_RecorderPlacementScopeDevice, AScopeBufferAndItsTrackedNodesLandOnDeviceOne)
    {
        const Runtime& rt = runtime();
        if (rt.advanced().device_count() < 2)
            GTEST_SKIP() << "needs a second device for the rule's target";
        ASSERT_TRUE(rt.set_placement_rules(send_to(1, AbsentAction::Fail)));
        const PlacementScope scope = rt.placement_scope(K_CLASS);
        auto data = scope.buffer<std::uint32_t>(K_COUNT).value();
        std::uint32_t* words = data.data();
        auto graph = rt.graph();

        graph.recorder().within(
            scope,
            [&](Recorder& in_scope)
            {
                in_scope.kernel(K_COUNT, Access(Reads(), Writes(data)),
                                [words](std::size_t i)
                                { words[i] = static_cast<std::uint32_t>(i) * 2u; });
                in_scope.untracked_kernel(K_COUNT, [](std::size_t) {}, On(scope));
            });

        EXPECT_EQ(data.device_index().value(), 1u);
        EXPECT_EQ(graph.recorder().refusal(), nullptr);
        ASSERT_TRUE(graph.run());
        for (std::size_t i = 0; i < K_COUNT; ++i)
            ASSERT_EQ(words[i], i * 2u) << "element " << i;
    }
} // namespace PlacementScopeDeviceRecordingTests

namespace PlacedTaskTests
{
    using namespace SushiRuntime;

    /** @brief Fixture of the RecorderPlacedTask tests, which run on the program's runtime. */
    class Medium_RecorderPlacedTask : public RecorderTest
    {
    };

    /** @brief Tests that a placed task over borrowed memory gets the queue of its device. */
    TEST_F(Medium_RecorderPlacedTask, ATaskOverBorrowedMemoryRunsOnTheDeviceItStates)
    {
        const Runtime& rt = runtime();
        const std::size_t device = rt.advanced().device_count() - 1;
        std::vector<std::uint32_t> memory(4, 0u);
        const LivenessToken owner = make_liveness();
        const Buffer<std::uint32_t> borrowed = rt.interop().borrow(
            memory.data(), memory.size(), DeviceIndex{device}, Residency::Host, owner).value();

        std::optional<sycl::device> ran_on;
        auto graph = rt.graph();
        graph.recorder().task(
            Access(Reads(), Writes(borrowed)),
            [&ran_on](sycl::queue& queue, const std::vector<sycl::event>&)
            {
                ran_on = queue.get_device();
                return sycl::event{};
            },
            On(DeviceIndex{device}));
        ASSERT_TRUE(graph.run());

        ASSERT_TRUE(ran_on.has_value());
        EXPECT_EQ(*ran_on,
                  rt.advanced().queue(DeviceIndex{device}).get_device());
    }

    /** @brief Fixture of the RecorderPlacedTaskDeathTest tests, which run on the program's runtime. */
    class Medium_RecorderPlacedTaskDeathTest : public RecorderTest
    {
    };

    /** @brief Tests that a task placed on a device the runtime lacks is refused at once. */
    TEST_F(Medium_RecorderPlacedTaskDeathTest, ATaskPlacedPastTheDeviceCountStops)
    {
        const Runtime& rt = runtime();
        const std::size_t count = rt.advanced().device_count();
        auto graph = rt.graph();
        EXPECT_DEATH(graph.recorder().task(
                         Access(),
                         [](sycl::queue&, const std::vector<sycl::event>&)
                         {
                             return sycl::event{};
                         },
                         On(DeviceIndex{count})),
                     "names no device");
    }

    /**
     * @brief Returns a device other than device 0 whose context is, or is not, device 0's.
     * @param shared Whether the device must share device 0's context.
     */
    std::optional<std::size_t> other_device(const Runtime& rt, bool shared)
    {
        const auto& devices = rt.advanced();
        for (std::size_t device = 1; device < devices.device_count(); ++device)
            if ((devices.queue(DeviceIndex{device}).get_context() ==
                 devices.queue(DeviceIndex{0}).get_context()) == shared)
                return device;
        return std::nullopt;
    }

    /** @brief Returns a body that notes the device of the queue it is handed. */
    auto noting_body(std::optional<sycl::device>& ran_on)
    {
        return [&ran_on](sycl::queue& queue, const std::vector<sycl::event>&)
        {
            ran_on = queue.get_device();
            return sycl::event{};
        };
    }

    /** @brief Tests that a stated device in the owner's context wins over the owner. */
    TEST_F(Medium_RecorderPlacedTask, AStatedDeviceInTheOwnersContextWinsOverTheOwner)
    {
        const Runtime& rt = runtime();
        const std::optional<std::size_t> device = other_device(rt, true);
        if (!device)
            GTEST_SKIP() << "no second device shares device 0's context";

        const Buffer<std::uint32_t> owned = rt.buffer<std::uint32_t>(4).value();
        std::optional<sycl::device> ran_on;
        auto graph = rt.graph();
        graph.recorder().task(Access(Reads(), Writes(owned)), noting_body(ran_on),
                              On(DeviceIndex{*device}));
        ASSERT_TRUE(graph.run());

        ASSERT_TRUE(ran_on.has_value());
        EXPECT_EQ(*ran_on,
                  rt.advanced().queue(DeviceIndex{*device}).get_device());
    }

    /** @brief Tests that a stated device in another context than the owner's is refused. */
    TEST_F(Medium_RecorderPlacedTask, AStatedDeviceInAnotherContextIsRefused)
    {
        const Runtime& rt = runtime();
        const std::optional<std::size_t> device = other_device(rt, false);
        if (!device)
            GTEST_SKIP() << "no device lies outside device 0's context";

        const Buffer<std::uint32_t> owned = rt.buffer<std::uint32_t>(4).value();
        std::optional<sycl::device> ran_on;
        auto graph = rt.graph();
        graph.recorder().task(Access(Reads(), Writes(owned)), noting_body(ran_on),
                              On(DeviceIndex{*device}));
        const auto refused = graph.run();

        ASSERT_FALSE(refused);
        EXPECT_EQ(refused.error().code, ErrorCode::InvalidGraph);
        EXPECT_NE(refused.error().message.find("another SYCL context"), std::string::npos)
            << refused.error().message;
        EXPECT_FALSE(ran_on.has_value()) << "a refused node is never moved to the owner";
    }

    /** @brief Tests that a task that states no device runs on the owner of its memory. */
    TEST_F(Medium_RecorderPlacedTask, ATaskThatStatesNoDeviceRunsOnTheOwner)
    {
        const Runtime& rt = runtime();
        const std::size_t device = rt.advanced().device_count() - 1;
        const Buffer<std::uint32_t> owned =
            rt.buffer<std::uint32_t>(4, DeviceIndex{device}).value();

        std::optional<sycl::device> ran_on;
        auto graph = rt.graph();
        graph.recorder().task(Access(Reads(), Writes(owned)), noting_body(ran_on));
        ASSERT_TRUE(graph.run());

        ASSERT_TRUE(ran_on.has_value());
        EXPECT_EQ(*ran_on,
                  rt.advanced().queue(DeviceIndex{device}).get_device());
    }

    /** @brief Returns a rule set whose one rule sends class "recorder.ruled" to a device index. */
    PlacementRules rule_to_device(std::size_t device, AbsentAction if_absent)
    {
        PlacementRule rule;
        rule.name = "ruled";
        rule.match.placement_class = "recorder.ruled";
        rule.target.device_index = device;
        rule.if_absent = AbsentPolicy{if_absent, std::chrono::milliseconds{0}};
        PlacementRules rules;
        EXPECT_TRUE(rules.add(std::move(rule)));
        return rules;
    }

    /** @brief Records one task that states no device inside the scope the rule matches. */
    void record_ruled_task(const Runtime& rt, Graph& graph, std::optional<sycl::device>& ran_on)
    {
        graph.recorder().within(rt.placement_scope("recorder.ruled"),
                                [&ran_on](Recorder& in_scope)
                                { in_scope.task(Access(), noting_body(ran_on)); });
    }

    /** @brief Fixture of the RecorderRuledTask tests, which run on the program's runtime. */
    class Medium_RecorderRuledTask : public RecorderTest
    {
    };

    /** @brief Tests that a rule naming device 1 of this machine runs its node on device 1. */
    TEST_F(Medium_RecorderRuledTask, Regression_ARuleNamingDeviceOneRunsTheNodeThere)
    {
        const Runtime& rt = runtime();
        if (rt.advanced().device_count() < 2)
            GTEST_SKIP() << "needs a second device for the rule's target";
        ASSERT_TRUE(rt.set_placement_rules(rule_to_device(1, AbsentAction::Fail)));

        std::optional<sycl::device> ran_on;
        auto graph = rt.graph();
        record_ruled_task(rt, graph, ran_on);
        ASSERT_TRUE(graph.run());

        ASSERT_TRUE(ran_on.has_value());
        EXPECT_EQ(*ran_on, rt.advanced().queue(DeviceIndex{1}).get_device());
    }

    /** @brief Tests that a rule naming a device this machine lacks runs its node on device 0 when it says Local. */
    TEST_F(Medium_RecorderRuledTask, Regression_ARuleNamingAnAbsentDeviceRunsLocalWhenItSaysLocal)
    {
        const Runtime& rt = runtime();
        const std::size_t absent = rt.advanced().device_count();

        ASSERT_TRUE(rt.set_placement_rules(rule_to_device(absent, AbsentAction::Local)));
        std::optional<sycl::device> ran_on;
        auto local = rt.graph();
        record_ruled_task(rt, local, ran_on);
        ASSERT_TRUE(local.run());
        ASSERT_TRUE(ran_on.has_value());
        EXPECT_EQ(*ran_on, rt.advanced().queue(DeviceIndex{0}).get_device());
    }

    /** @brief Tests that a rule naming a device this machine lacks refuses its run when it says Fail. */
    TEST_F(Medium_RecorderRuledTask, Regression_ARuleNamingAnAbsentDeviceRefusesWhenItSaysFail)
    {
        const Runtime& rt = runtime();
        const std::size_t absent = rt.advanced().device_count();

        ASSERT_TRUE(rt.set_placement_rules(rule_to_device(absent, AbsentAction::Fail)));
        std::optional<sycl::device> ran_on;
        auto failing = rt.graph();
        record_ruled_task(rt, failing, ran_on);
        const auto refused = failing.run();
        ASSERT_FALSE(refused);
        EXPECT_EQ(refused.error().code, ErrorCode::InvalidArgument);
        EXPECT_NE(refused.error().message.find("\"ruled\""), std::string::npos)
            << refused.error().message;
        EXPECT_FALSE(ran_on.has_value()) << "a refused compile runs nothing";
    }
} // namespace PlacedTaskTests
