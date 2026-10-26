/**************************************************************************/
/* test_collect_profiling.cpp                                             */
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
 * @file test_collect_profiling.cpp
 * @brief Tests what a run's observation holds: the node table, the records, the pairings and the drops.
 * @see tests/surface/profiling/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <memory>
#include <set>
#include <string>

#include <gtest/gtest.h>

#include <SushiLog/log/log_level.hpp>

#include <SushiRuntime/report/run_observer.hpp>
#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/test_runner/log_capture.hpp>
#include <SushiRuntime/test_runner/recording_observer.hpp>

#include "observed_pipeline.hpp"

using namespace SushiRuntime;
using ObserverTests::ObservedPipeline;
using TestRunner::RecordingObserver;

namespace
{
    /** @brief Runs the pipeline once with an observer added and returns what it saw. */
    RunObservation observe_one_run(ObservedPipeline& pipeline)
    {
        const auto observer = std::make_shared<RecordingObserver>();
        const RunObserverHandle handle = pipeline.runtime().add_run_observer(observer);
        EXPECT_TRUE(pipeline.graph().run().has_value());
        EXPECT_EQ(observer->count(), 1u);
        return observer->count() == 1 ? observer->last() : RunObservation{};
    }
} // namespace

/** @brief Tests that every record joins a table row by its node index, with the name, operation and cost stated. */
TEST(Medium_CollectProfiling, TheTableAndTheRecordsJoinByNodeIndex)
{
    ObservedPipeline pipeline;

    const RunObservation observation = observe_one_run(pipeline);

    ASSERT_NE(observation.table, nullptr);
    ASSERT_EQ(observation.table->size(), 3u);
    std::set<std::string> names;
    for (const NodeEventRecord& record : observation.events)
    {
        const NodeTableRow* row = observation.table->find(record.node_index);
        ASSERT_NE(row, nullptr) << "node " << record.node_index;
        names.insert(row->name);
    }
    EXPECT_EQ(names, (std::set<std::string>{"load", "scale", "store"}));

    const NodeTableRow* load = observation.table->find(ObserverTests::K_LOAD);
    EXPECT_EQ(load->name, "load");
    EXPECT_EQ(load->operation.value, ("pipeline.load"_op).value);
    ASSERT_TRUE(load->cost.has_value());
    EXPECT_EQ(load->cost->flops, ObservedPipeline::K_ELEMENTS);
    EXPECT_EQ(load->cost->dtype, NodeDType::F32);
    const NodeTableRow* scale = observation.table->find(ObserverTests::K_SCALE);
    ASSERT_TRUE(scale->cost.has_value()) << "a data-movement node states zero flops and real bytes";
    EXPECT_EQ(scale->cost->flops, 0u);
    EXPECT_GT(scale->cost->bytes, 0u);
    EXPECT_FALSE(observation.table->find(ObserverTests::K_STORE)->cost.has_value());
}

/** @brief Tests that each kernel record names a device the runtime has and that device's pairing is present. */
TEST(Medium_CollectProfiling, EveryDeviceRecordHasItsDevicesPairing)
{
    ObservedPipeline pipeline;
    const std::size_t devices = pipeline.runtime().advanced().device_count();

    const RunObservation observation = observe_one_run(pipeline);

    ASSERT_FALSE(observation.events.empty());
    for (const NodeEventRecord& record : observation.events)
    {
        ASSERT_NE(record.device_index, K_HOST_DEVICE_INDEX) << "a kernel node runs on a device queue";
        EXPECT_LT(record.device_index, devices);
        bool paired = false;
        for (const DeviceClock& clock : observation.device_clocks)
            if (clock.device_index == record.device_index)
                paired = true;
        EXPECT_TRUE(paired) << "device " << record.device_index << " has no pairing";
    }
}

/** @brief Tests that the table of a plan on the chain path holds the same rows as on the worker pool. */
TEST(Medium_CollectProfiling, APlanRunOnTheChainFillsTheSameTable)
{
    ObservedPipeline scheduled(ObservedPipeline::scheduler_config());
    ObservedPipeline chained(ObservedPipeline::chain_config());

    const RunObservation on_pool = observe_one_run(scheduled);
    const RunObservation on_chain = observe_one_run(chained);

    if (on_chain.layer != ExecutionLayer::Chain)
        GTEST_SKIP() << "this device's plan does not classify as a chain";
    ASSERT_NE(on_chain.table, nullptr);
    ASSERT_EQ(on_chain.table->size(), on_pool.table->size());
    for (const NodeTableRow& row : on_pool.table->rows())
    {
        const NodeTableRow* other = on_chain.table->find(row.index);
        ASSERT_NE(other, nullptr);
        EXPECT_EQ(other->name, row.name);
        EXPECT_EQ(other->operation.value, row.operation.value);
        EXPECT_EQ(other->cost.has_value(), row.cost.has_value());
    }
    EXPECT_EQ(on_chain.events.size(), on_pool.events.size());
}

/** @brief Tests that a run that lost no dispatch carries zero drops and logs nothing. */
TEST(Medium_CollectProfiling, CarriesNoDropWhenNoneWasLost)
{
    ObservedPipeline pipeline;
    TestRunner::LogCapture capture;

    const RunObservation observation = observe_one_run(pipeline);

    EXPECT_FALSE(observation.drops.any());
    EXPECT_EQ(observation.drops.total(), 0u);
    EXPECT_EQ(capture.count_in("execution"), 0u) << "a run that loses none logs nothing";
}

/** @brief Tests that a run longer than its ledger was sized for carries the drops and logs one WARN. */
TEST(Medium_CollectProfiling, CarriesTheDropCountsOfTheRun)
{
    ObservedPipeline pipeline;
    const auto observer = std::make_shared<RecordingObserver>();
    const RunObserverHandle handle = pipeline.runtime().add_run_observer(observer);
    TestRunner::LogCapture capture;

    // run_until sizes the ledger for one step, so a run of six overflows every node's row.
    int calls = 0;
    ASSERT_TRUE(pipeline.graph().run_until([&calls]() { return ++calls > 6; }).has_value());

    ASSERT_EQ(observer->count(), 1u);
    const RunObservation observation = observer->last();
    EXPECT_TRUE(observation.drops.any());
    EXPECT_GT(observation.drops.of(DropCause::FullRow), 0u);
    EXPECT_EQ(observation.drops.of(DropCause::FullRow) + observation.events.size(), 18u)
        << "three nodes over six steps: each dispatch is a record or a counted drop";
    EXPECT_EQ(capture.count_at(SushiLog::LogLevel::Warn), 1u) << "one line for the run, not one per dispatch";
    EXPECT_TRUE(capture.has_message(SushiLog::LogLevel::Warn, "full ledger row"));
}

/** @brief Tests that the run after an overflow sizes its ledger for the demand and loses nothing. */
TEST(Medium_CollectProfiling, TheRunAfterAnOverflowLosesNothing)
{
    ObservedPipeline pipeline;
    const auto observer = std::make_shared<RecordingObserver>();
    const RunObserverHandle handle = pipeline.runtime().add_run_observer(observer);
    int calls = 0;
    ASSERT_TRUE(pipeline.graph().run_until([&calls]() { return ++calls > 6; }).has_value());
    ASSERT_TRUE(observer->last().drops.any());

    calls = 0;
    ASSERT_TRUE(pipeline.graph().run_until([&calls]() { return ++calls > 6; }).has_value());

    ASSERT_EQ(observer->count(), 2u);
    EXPECT_FALSE(observer->last().drops.any()) << "the ledger grew to the demand it saw";
    EXPECT_EQ(observer->last().events.size(), 18u);
}
