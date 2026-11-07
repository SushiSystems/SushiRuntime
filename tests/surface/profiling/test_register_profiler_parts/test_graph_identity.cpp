/**************************************************************************/
/* test_graph_identity.cpp                                                */
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
 * @file test_graph_identity.cpp
 * @brief Tests what tells one graph's runs from another's in a profile: its label, its id and its nodes' origin.
 * @see tests/surface/profiling/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/report/run_observer.hpp>
#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/report/recording_observer.hpp>

#include "observed_pipeline.hpp"
#include "support/host_runtime.hpp"

using namespace SushiRuntime;
using ObserverTests::ObservedPipeline;
using SushiRuntime::RecordingObserver;

namespace
{
    constexpr std::size_t K_N = 32;

    /** @brief Records one kernel node into @p graph over @p buffer. */
    void add_fill(Graph& graph, Buffer<float>& buffer, const char* name)
    {
        float* data = buffer.data();
        NodeOptions options;
        options.name = name;
        graph.add(K_N, Reads{}, Writes(buffer), [data](std::size_t i) { data[i] = static_cast<float>(i); }, options);
    }
} // namespace

namespace
{
    /** @brief Returns the label of the table a run of a graph labelled @p label is observed with; null label sets none. */
    std::string observed_label_of_a_graph(Runtime& runtime, const char* label)
    {
        ObservedPipeline pipeline(runtime);
        if (label != nullptr)
            pipeline.graph().set_label(label);
        const auto observer = std::make_shared<RecordingObserver>();
        const RunObserverHandle handle = pipeline.runtime().add_run_observer(observer);

        EXPECT_TRUE(pipeline.graph().run().has_value());

        EXPECT_EQ(observer->count(), 1u);
        EXPECT_NE(observer->last().table, nullptr);
        return observer->last().table->label();
    }

    /** @brief Returns the label of the table a run of a dynamic graph labelled @p label is observed with. */
    std::string observed_label_of_a_dynamic_graph(Runtime& runtime, const char* label)
    {
        auto buffer = runtime.buffer<float>(K_N).value();
        float* data = buffer.data();
        auto world = runtime.dynamic_graph();
        world.set_label(label);
        world.region(0).add(K_N, Reads{}, Writes(buffer), [data](std::size_t i) { data[i] = 1.0f; });
        const auto observer = std::make_shared<RecordingObserver>();
        const RunObserverHandle handle = runtime.add_run_observer(observer);

        EXPECT_TRUE(world.run().has_value());

        EXPECT_EQ(observer->count(), 1u);
        return observer->last().table->label();
    }
} // namespace

/** @brief Fixture of the tests of the label of a graph and of its table, run on a host runtime. */
class Small_GraphLabel : public TestSupport::HostRuntimeTest
{
};

/** @brief Fixture of the tests of the options a node is recorded with, run on a host runtime. */
class Small_NodeOptions : public TestSupport::HostRuntimeTest
{
};

/** @brief Fixture of the tests of the id of a graph, run on a host runtime. */
class Small_GraphId : public TestSupport::HostRuntimeTest
{
};

/** @brief Tests that the label of a graph, static or dynamic, is the label of the table its runs are observed with, and none is empty. */
TEST_F(Small_GraphLabel, TheTableCarriesTheLabelOfItsGraph)
{
    EXPECT_EQ(observed_label_of_a_graph(runtime(), "encoder"), "encoder");
    EXPECT_EQ(observed_label_of_a_graph(runtime(), nullptr), "") << "a graph with no label has an empty one";
    EXPECT_EQ(observed_label_of_a_dynamic_graph(runtime(), "world"), "world");
}

/** @brief Tests that a changed label reaches the table at the next build and forces no rebuild of its own. */
TEST_F(Small_GraphLabel, ALabelChangeAppliesAtTheNextBuildAndForcesNone)
{
    ObservedPipeline pipeline(runtime());
    pipeline.graph().set_label("first");
    const auto observer = std::make_shared<RecordingObserver>();
    const RunObserverHandle handle = pipeline.runtime().add_run_observer(observer);
    ASSERT_TRUE(pipeline.graph().run().has_value());
    ASSERT_EQ(pipeline.graph().compile_count(), 1u);

    pipeline.graph().set_label("second");
    ASSERT_TRUE(pipeline.graph().run().has_value());

    EXPECT_EQ(pipeline.graph().compile_count(), 1u) << "naming a graph is not a reason to rebuild its plan";
    EXPECT_EQ(observer->last().table->label(), "first") << "the plan in use keeps the label it was built with";

    // A new node makes the plan stale, so the next build takes the new label.
    auto extra = pipeline.runtime().buffer<float>(K_N).value();
    add_fill(pipeline.graph(), extra, "extra");
    ASSERT_TRUE(pipeline.graph().run().has_value());
    EXPECT_EQ(pipeline.graph().compile_count(), 2u);
    EXPECT_EQ(observer->last().table->label(), "second");
}

/** @brief Tests that a dynamic graph takes a label too. */
TEST_F(Small_GraphLabel, ADynamicGraphCarriesItsLabel)
{
    auto buffer = runtime().buffer<float>(K_N).value();
    float* data = buffer.data();
    auto world = runtime().dynamic_graph();
    world.set_label("world");
    world.region(0).add(K_N, Reads{}, Writes(buffer), [data](std::size_t i) { data[i] = 1.0f; });
    const auto observer = std::make_shared<RecordingObserver>();
    const RunObserverHandle handle = runtime().add_run_observer(observer);

    ASSERT_TRUE(world.run().has_value());

    ASSERT_EQ(observer->count(), 1u);
    EXPECT_EQ(observer->last().table->label(), "world");
}

/** @brief Tests that the origin a caller states is copied when the node is recorded, so its storage may go. */
TEST_F(Small_NodeOptions, OriginIsCopiedAtRecordTime)
{
    auto buffer = runtime().buffer<float>(K_N).value();
    float* data = buffer.data();
    auto graph = runtime().graph();
    {
        std::string origin = "attention";
        NodeOptions options;
        options.name = "q_proj";
        options.origin = origin;
        graph.add(K_N, Reads{}, Writes(buffer), [data](std::size_t i) { data[i] = 1.0f; }, options);
        origin.assign(origin.size(), 'x'); // the caller's storage changes and then goes
    }
    const auto observer = std::make_shared<RecordingObserver>();
    const RunObserverHandle handle = runtime().add_run_observer(observer);

    ASSERT_TRUE(graph.run().has_value());

    ASSERT_EQ(observer->count(), 1u);
    const NodeTableRow* row = observer->last().table->find(0);
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->origin, "attention");
}

namespace
{
    /** @brief Returns the names of the rows of @p table that state an origin. */
    std::vector<std::string> rows_stating_an_origin(const NodeTable& table)
    {
        std::vector<std::string> names;
        for (const NodeTableRow& row : table.rows())
            if (!row.origin.empty())
                names.push_back(row.name);
        return names;
    }
} // namespace

/** @brief Tests that a node that states no origin has an empty one. */
TEST_F(Small_NodeOptions, ANodeThatStatesNoOriginHasNone)
{
    ObservedPipeline pipeline(runtime());
    const auto observer = std::make_shared<RecordingObserver>();
    const RunObserverHandle handle = pipeline.runtime().add_run_observer(observer);

    ASSERT_TRUE(pipeline.graph().run().has_value());

    ASSERT_EQ(observer->count(), 1u);
    EXPECT_TRUE(rows_stating_an_origin(*observer->last().table).empty());
}

/** @brief Tests that a run carries the id of its graph, static or dynamic, and no graph shares or has the id 0. */
TEST_F(Small_GraphId, ARunCarriesTheIdOfItsGraph)
{
    auto first_buffer = runtime().buffer<float>(K_N).value();
    auto second_buffer = runtime().buffer<float>(K_N).value();
    auto world_buffer = runtime().buffer<float>(K_N).value();
    float* world_data = world_buffer.data();
    auto first = runtime().graph();
    auto second = runtime().graph();
    auto world = runtime().dynamic_graph();
    add_fill(first, first_buffer, "a");
    add_fill(second, second_buffer, "b");
    world.region(0).add(K_N, Reads{}, Writes(world_buffer), [world_data](std::size_t i) { world_data[i] = 1.0f; });
    const auto observer = std::make_shared<RecordingObserver>();
    const RunObserverHandle handle = runtime().add_run_observer(observer);

    ASSERT_TRUE(first.run().has_value());
    ASSERT_TRUE(second.run().has_value());
    ASSERT_TRUE(first.run().has_value());
    ASSERT_TRUE(world.run().has_value());

    const std::vector<RunObservation> runs = observer->runs();
    ASSERT_EQ(runs.size(), 4u);
    EXPECT_NE(first.id(), second.id());
    EXPECT_NE(world.id(), first.id());
    EXPECT_NE(world.id(), 0u);
    EXPECT_EQ(runs[0].graph_id, first.id());
    EXPECT_EQ(runs[1].graph_id, second.id());
    EXPECT_EQ(runs[2].graph_id, first.id());
    EXPECT_EQ(runs[3].graph_id, world.id());
    EXPECT_EQ(runs[0].table->graph_id(), first.id()) << "the table of a plan names the graph it was built for";
}

/** @brief Tests that an async step is observed under the id of the graph that started it. */
TEST_F(Small_GraphId, AnAsyncStepCarriesTheIdOfItsGraph)
{
    ObservedPipeline pipeline(runtime());
    const auto observer = std::make_shared<RecordingObserver>();
    const RunObserverHandle handle = pipeline.runtime().add_run_observer(observer);

    auto step = pipeline.graph().run_async().value();
    ASSERT_TRUE(step.wait().has_value());

    ASSERT_EQ(observer->count(), 1u);
    EXPECT_EQ(observer->last().graph_id, pipeline.graph().id());
}

/** @brief Tests that a graph keeps its id across a move and the moved-from one reports 0. */
TEST_F(Small_GraphId, AMovedFromGraphReportsZero)
{
    auto original = runtime().graph();
    const std::uint64_t id = original.id();
    ASSERT_NE(id, 0u);

    Graph moved = std::move(original);

    EXPECT_EQ(moved.id(), id);
    EXPECT_EQ(original.id(), 0u);
}

namespace
{
    /** @brief Returns the ids of @p count graphs made on @p runtime. */
    std::set<std::uint64_t> ids_of_new_graphs(Runtime& runtime, int count)
    {
        std::set<std::uint64_t> ids;
        for (int i = 0; i < count; ++i)
            ids.insert(runtime.graph().id());
        return ids;
    }
} // namespace

/** @brief Tests that every graph takes its own id and none takes 0. */
TEST_F(Small_GraphId, NoGraphHasIdZero)
{

    const std::set<std::uint64_t> ids = ids_of_new_graphs(runtime(), 8);

    EXPECT_EQ(ids.size(), 8u) << "every graph takes its own id";
    EXPECT_EQ(ids.count(0), 0u);
}
