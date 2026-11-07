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
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiLog/log/log_level.hpp>

#include <SushiRuntime/report/run_observer.hpp>
#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/test_runner/log_capture.hpp>
#include <SushiRuntime/report/recording_observer.hpp>

#include "observed_pipeline.hpp"
#include "support/runtime_environment.hpp"

using namespace SushiRuntime;
using ObserverTests::ObservedPipeline;
using SushiRuntime::RecordingObserver;

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

namespace
{
    /** @brief Returns whether @p observation holds a device clock for @p device. */
    bool has_pairing(const RunObservation& observation, std::uint32_t device)
    {
        for (const DeviceClock& clock : observation.device_clocks)
            if (clock.device_index == device)
                return true;
        return false;
    }

    /** @brief Returns the names of the table rows the records of @p observation join; a record with no row adds "?". */
    std::set<std::string> joined_names(const RunObservation& observation)
    {
        std::set<std::string> names;
        for (const NodeEventRecord& record : observation.events)
        {
            const NodeTableRow* row = observation.table->find(record.node_index);
            names.insert(row != nullptr ? row->name : std::string("?"));
        }
        return names;
    }

    /** @brief Expects every row of @p expected to be in @p actual with the same name, operation and cost presence. */
    void expect_same_rows(const NodeTable& expected, const NodeTable& actual)
    {
        for (const NodeTableRow& row : expected.rows())
        {
            SCOPED_TRACE(row.name);
            const NodeTableRow* other = actual.find(row.index);
            ASSERT_NE(other, nullptr);
            EXPECT_EQ(other->name, row.name);
            EXPECT_EQ(other->operation.value, row.operation.value);
            EXPECT_EQ(other->cost.has_value(), row.cost.has_value());
        }
    }

    /** @brief Expects every device record of @p observation, @p records of them, to have its device's pairing. */
    void expect_every_device_record_paired(const RunObservation& observation, std::size_t records)
    {
        ASSERT_EQ(observation.events.size(), records);
        for (const NodeEventRecord& record : observation.events)
        {
            ASSERT_NE(record.device_index, K_HOST_DEVICE_INDEX) << "a kernel or a task body runs on a device queue";
            EXPECT_TRUE(has_pairing(observation, record.device_index))
                << "device " << record.device_index << " has no pairing, so a profiler drops the record";
        }
    }
} // namespace

/** @brief Tests that every record joins a table row by its node index, and the rows are the nodes the caller named. */
TEST(Medium_CollectProfiling, TheTableAndTheRecordsJoinByNodeIndex)
{
    ObservedPipeline pipeline;

    const RunObservation observation = observe_one_run(pipeline);

    ASSERT_NE(observation.table, nullptr);
    ASSERT_EQ(observation.table->size(), 3u);
    EXPECT_EQ(joined_names(observation), (std::set<std::string>{"load", "scale", "store"}));
}

/** @brief Tests that the table rows carry the operation and the cost the caller stated, and no cost where none was. */
TEST(Medium_CollectProfiling, TheTableCarriesTheOperationAndTheCostTheCallerStated)
{
    ObservedPipeline pipeline;

    const RunObservation observation = observe_one_run(pipeline);

    ASSERT_NE(observation.table, nullptr);
    const NodeTableRow* load = observation.table->find(ObserverTests::K_LOAD);
    ASSERT_NE(load, nullptr);
    EXPECT_EQ(load->operation.value, ("pipeline.load"_op).value);
    ASSERT_TRUE(load->cost.has_value());
    EXPECT_EQ(load->cost->flops, ObservedPipeline::K_ELEMENTS);
    EXPECT_EQ(load->cost->dtype, NodeDType::F32);
    const NodeTableRow* scale = observation.table->find(ObserverTests::K_SCALE);
    ASSERT_NE(scale, nullptr);
    ASSERT_TRUE(scale->cost.has_value()) << "a data-movement node states zero flops and real bytes";
    EXPECT_EQ(scale->cost->flops, 0u);
    EXPECT_GT(scale->cost->bytes, 0u);
    const NodeTableRow* store = observation.table->find(ObserverTests::K_STORE);
    ASSERT_NE(store, nullptr);
    EXPECT_FALSE(store->cost.has_value());
}

/** @brief Tests that each kernel record of a graph names a device the pairing of which is present. */
TEST(Medium_CollectProfiling, KernelNodesClaimTheirDevicesPairing)
{
    ObservedPipeline pipeline;

    const RunObservation observation = observe_one_run(pipeline);

    expect_every_device_record_paired(observation, 3u);
}

/** @brief Tests that the table of a plan on the chain path holds the same rows as on the worker pool. */
TEST(Medium_CollectProfiling, APlanRunOnTheChainFillsTheSameTable)
{
    ObservedPipeline scheduled(ObservedPipeline::scheduler_config());
    ObservedPipeline chained(ObservedPipeline::chain_config());

    const RunObservation on_pool = observe_one_run(scheduled);
    const RunObservation on_chain = observe_one_run(chained);

    ASSERT_NE(on_chain.table, nullptr);
    ASSERT_NE(on_pool.table, nullptr);
    ASSERT_EQ(on_chain.table->size(), on_pool.table->size());
    expect_same_rows(*on_pool.table, *on_chain.table);
    EXPECT_EQ(on_chain.events.size(), on_pool.events.size());
}

/** @brief Tests that a run that lost no dispatch carries zero drops. */
TEST(Medium_CollectProfiling, CarriesNoDropWhenNoneWasLost)
{
    ObservedPipeline pipeline;

    const RunObservation observation = observe_one_run(pipeline);

    EXPECT_FALSE(observation.drops.any());
    EXPECT_EQ(observation.drops.total(), 0u);
}

/** @brief Tests that a run that lost no dispatch logs nothing. */
TEST(Medium_CollectProfiling, ALossFreeRunLogsNothing)
{
    ObservedPipeline pipeline;
    TestRunner::LogCapture capture;

    static_cast<void>(observe_one_run(pipeline));

    EXPECT_EQ(capture.count_in("execution"), 0u);
}

namespace
{
    /** @brief Runs the pipeline's graph for six steps,, which overflows a ledger sized for one. */
    void run_six_steps(ObservedPipeline& pipeline)
    {
        int calls = 0;
        ASSERT_TRUE(pipeline.graph().run_until([&calls]() { return ++calls > 6; }).has_value());
    }
} // namespace

/** @brief Tests that a run longer than its ledger was sized for carries the drops, and a drop plus a record is a dispatch. */
TEST(Medium_CollectProfiling, CarriesTheDropCountsOfTheRun)
{
    ObservedPipeline pipeline;
    const auto observer = std::make_shared<RecordingObserver>();
    const RunObserverHandle handle = pipeline.runtime().add_run_observer(observer);

    // run_until sizes the ledger for one step, so a run of six overflows every node's row.
    run_six_steps(pipeline);

    ASSERT_EQ(observer->count(), 1u);
    const RunObservation observation = observer->last();
    EXPECT_TRUE(observation.drops.any());
    EXPECT_GT(observation.drops.of(DropCause::FullRow), 0u);
    EXPECT_EQ(observation.drops.of(DropCause::FullRow) + observation.events.size(), 18u)
        << "three nodes over six steps: each dispatch is a record or a counted drop";
}

/** @brief Tests that a run which overflowed its ledger logs one WARN line for the run, not one per dispatch. */
TEST(Medium_CollectProfiling, AnOverflowedRunLogsOneWarn)
{
    ObservedPipeline pipeline;
    const auto observer = std::make_shared<RecordingObserver>();
    const RunObserverHandle handle = pipeline.runtime().add_run_observer(observer);
    TestRunner::LogCapture capture;

    run_six_steps(pipeline);

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

namespace
{
    /** @brief Runs two task nodes, each submitting its own kernel, once on the program's runtime of @p shape; returns what the observer saw. */
    RunObservation observe_task_bodies(const TestSupport::RuntimeShape& shape)
    {
        constexpr std::size_t K_ELEMENTS = 256;
        Runtime& runtime = TestSupport::RuntimeEnvironment::runtime(shape);
        auto first = runtime.buffer<float>(K_ELEMENTS).value();
        auto second = runtime.buffer<float>(K_ELEMENTS).value();
        float* a = first.data();
        float* b = second.data();
        auto graph = runtime.graph();
        graph.add_task(Reads{}, Writes(first),
                       [a](sycl::queue& queue, const std::vector<sycl::event>& after)
                       {
                           return queue.parallel_for(sycl::range<1>(K_ELEMENTS), after,
                                                     [a](sycl::id<1> i) { a[i] = static_cast<float>(i[0]); });
                       });
        graph.add_task(Reads(first), Writes(second),
                       [a, b](sycl::queue& queue, const std::vector<sycl::event>& after)
                       {
                           return queue.parallel_for(sycl::range<1>(K_ELEMENTS), after,
                                                     [a, b](sycl::id<1> i) { b[i] = a[i] + 1.0f; });
                       });

        const auto observer = std::make_shared<RecordingObserver>();
        const RunObserverHandle handle = runtime.add_run_observer(observer);
        EXPECT_TRUE(graph.run().has_value());
        EXPECT_EQ(observer->count(), 1u);
        return observer->count() == 1 ? observer->last() : RunObservation{};
    }
} // namespace

/** @brief Tests that a plan of task bodies alone, run on the worker pool, pairs the device its records name. */
TEST(Medium_CollectProfiling, TaskBodiesOnTheWorkerPoolClaimTheirDevicesPairing)
{
    const RunObservation observation = observe_task_bodies(ObservedPipeline::scheduler_config());

    EXPECT_EQ(observation.layer, ExecutionLayer::Scheduler);
    expect_every_device_record_paired(observation, 2u);
}

/** @brief Tests that a plan of task bodies alone, run as a chain, pairs the device its records name. */
TEST(Medium_CollectProfiling, TaskBodiesOnTheChainClaimTheirDevicesPairing)
{
    const RunObservation observation = observe_task_bodies(ObservedPipeline::chain_config());

    expect_every_device_record_paired(observation, 2u);
}
