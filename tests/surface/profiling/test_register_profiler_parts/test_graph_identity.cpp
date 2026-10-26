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
#include <SushiRuntime/test_runner/recording_observer.hpp>

#include "observed_pipeline.hpp"

using namespace SushiRuntime;
using ObserverTests::ObservedPipeline;
using TestRunner::RecordingObserver;

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

/** @brief Tests that a label set on a graph is the label of the table its runs are observed with. */
TEST(Medium_GraphLabel, TheTableCarriesTheLabelOfItsGraph)
{
    ObservedPipeline pipeline;
    pipeline.graph().set_label("encoder");
    const auto observer = std::make_shared<RecordingObserver>();
    const RunObserverHandle handle = pipeline.runtime().add_run_observer(observer);

    ASSERT_TRUE(pipeline.graph().run().has_value());

    ASSERT_EQ(observer->count(), 1u);
    ASSERT_NE(observer->last().table, nullptr);
    EXPECT_EQ(observer->last().table->label(), "encoder");
}

/** @brief Tests that a graph with no label has an empty one. */
TEST(Medium_GraphLabel, AGraphWithNoLabelHasAnEmptyOne)
{
    ObservedPipeline pipeline;
    const auto observer = std::make_shared<RecordingObserver>();
    const RunObserverHandle handle = pipeline.runtime().add_run_observer(observer);

    ASSERT_TRUE(pipeline.graph().run().has_value());

    EXPECT_TRUE(observer->last().table->label().empty());
}

/** @brief Tests that a changed label reaches the table at the next build and forces no rebuild of its own. */
TEST(Medium_GraphLabel, ALabelChangeAppliesAtTheNextBuildAndForcesNone)
{
    ObservedPipeline pipeline;
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
TEST(Medium_GraphLabel, ADynamicGraphCarriesItsLabel)
{
    auto runtime = Runtime::create().value();
    auto buffer = runtime.buffer<float>(K_N).value();
    float* data = buffer.data();
    auto world = runtime.dynamic_graph();
    world.set_label("world");
    world.region(0).add(K_N, Reads{}, Writes(buffer), [data](std::size_t i) { data[i] = 1.0f; });
    const auto observer = std::make_shared<RecordingObserver>();
    const RunObserverHandle handle = runtime.add_run_observer(observer);

    ASSERT_TRUE(world.run().has_value());

    ASSERT_EQ(observer->count(), 1u);
    EXPECT_EQ(observer->last().table->label(), "world");
}

/** @brief Tests that the origin a caller states is copied when the node is recorded, so its storage may go. */
TEST(Medium_NodeOptions, OriginIsCopiedAtRecordTime)
{
    auto runtime = Runtime::create().value();
    auto buffer = runtime.buffer<float>(K_N).value();
    float* data = buffer.data();
    auto graph = runtime.graph();
    {
        std::string origin = "attention";
        NodeOptions options;
        options.name = "q_proj";
        options.origin = origin;
        graph.add(K_N, Reads{}, Writes(buffer), [data](std::size_t i) { data[i] = 1.0f; }, options);
        origin.assign(origin.size(), 'x'); // the caller's storage changes and then goes
    }
    const auto observer = std::make_shared<RecordingObserver>();
    const RunObserverHandle handle = runtime.add_run_observer(observer);

    ASSERT_TRUE(graph.run().has_value());

    ASSERT_EQ(observer->count(), 1u);
    const NodeTableRow* row = observer->last().table->find(0);
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->origin, "attention");
}

/** @brief Tests that a node that states no origin has an empty one. */
TEST(Medium_NodeOptions, ANodeThatStatesNoOriginHasNone)
{
    ObservedPipeline pipeline;
    const auto observer = std::make_shared<RecordingObserver>();
    const RunObserverHandle handle = pipeline.runtime().add_run_observer(observer);

    ASSERT_TRUE(pipeline.graph().run().has_value());

    for (const NodeTableRow& row : observer->last().table->rows())
        EXPECT_TRUE(row.origin.empty()) << row.name;
}

/** @brief Tests that two graphs on one runtime observe their own ids and never each other's. */
TEST(Medium_GraphId, TwoGraphsOnOneRuntimeObserveTheirOwnIds)
{
    auto runtime = Runtime::create().value();
    auto first_buffer = runtime.buffer<float>(K_N).value();
    auto second_buffer = runtime.buffer<float>(K_N).value();
    auto first = runtime.graph();
    auto second = runtime.graph();
    add_fill(first, first_buffer, "a");
    add_fill(second, second_buffer, "b");
    const auto observer = std::make_shared<RecordingObserver>();
    const RunObserverHandle handle = runtime.add_run_observer(observer);

    ASSERT_TRUE(first.run().has_value());
    ASSERT_TRUE(second.run().has_value());
    ASSERT_TRUE(first.run().has_value());

    const std::vector<RunObservation> runs = observer->runs();
    ASSERT_EQ(runs.size(), 3u);
    EXPECT_NE(first.id(), second.id());
    EXPECT_EQ(runs[0].graph_id, first.id());
    EXPECT_EQ(runs[1].graph_id, second.id());
    EXPECT_EQ(runs[2].graph_id, first.id());
    EXPECT_EQ(runs[0].table->graph_id(), first.id()) << "the table of a plan names the graph it was built for";
}

/** @brief Tests that a run of a dynamic graph carries the dynamic graph's id. */
TEST(Medium_GraphId, ADynamicGraphRunCarriesItsOwnId)
{
    auto runtime = Runtime::create().value();
    auto buffer = runtime.buffer<float>(K_N).value();
    float* data = buffer.data();
    auto world = runtime.dynamic_graph();
    world.region(0).add(K_N, Reads{}, Writes(buffer), [data](std::size_t i) { data[i] = 1.0f; });
    auto plain = runtime.graph();
    const auto observer = std::make_shared<RecordingObserver>();
    const RunObserverHandle handle = runtime.add_run_observer(observer);

    ASSERT_TRUE(world.run().has_value());

    ASSERT_EQ(observer->count(), 1u);
    EXPECT_EQ(observer->last().graph_id, world.id());
    EXPECT_NE(world.id(), plain.id());
    EXPECT_NE(world.id(), 0u);
}

/** @brief Tests that an async step is observed under the id of the graph that started it. */
TEST(Medium_GraphId, AnAsyncStepCarriesTheIdOfItsGraph)
{
    ObservedPipeline pipeline;
    const auto observer = std::make_shared<RecordingObserver>();
    const RunObserverHandle handle = pipeline.runtime().add_run_observer(observer);

    auto step = pipeline.graph().run_async().value();
    ASSERT_TRUE(step.wait().has_value());

    ASSERT_EQ(observer->count(), 1u);
    EXPECT_EQ(observer->last().graph_id, pipeline.graph().id());
}

/** @brief Tests that a graph keeps its id across a move and the moved-from one reports 0. */
TEST(Medium_GraphId, AMovedFromGraphReportsZero)
{
    auto runtime = Runtime::create().value();
    auto original = runtime.graph();
    const std::uint64_t id = original.id();
    ASSERT_NE(id, 0u);

    Graph moved = std::move(original);

    EXPECT_EQ(moved.id(), id);
    EXPECT_EQ(original.id(), 0u);
}

/** @brief Tests that a run with no known graph id would read 0, which no graph has. */
TEST(Medium_GraphId, NoGraphHasIdZero)
{
    auto runtime = Runtime::create().value();
    std::set<std::uint64_t> ids;
    for (int i = 0; i < 8; ++i)
        ids.insert(runtime.graph().id());

    EXPECT_EQ(ids.size(), 8u) << "every graph takes its own id";
    EXPECT_EQ(ids.count(0), 0u);
}
