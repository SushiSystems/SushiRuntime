/**************************************************************************/
/* test_run_observer.cpp                                                  */
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
 * @file test_run_observer.cpp
 * @brief Tests the observers a runtime fans its finished runs out to, and the device timing they request.
 * @see tests/surface/profiling/README.md
 * @author Mustafa Garip
 */

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>
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
    constexpr auto K_DELIVERY_TIMEOUT = std::chrono::milliseconds(5000);

    /** @brief Waits until @p observer has seen @p count runs and returns whether it did. */
    bool wait_for_runs(const RecordingObserver& observer, std::size_t count)
    {
        const auto deadline = std::chrono::steady_clock::now() + K_DELIVERY_TIMEOUT;
        while (observer.count() < count && std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        return observer.count() >= count;
    }
} // namespace

/** @brief Tests that an observer sees each blocking run once, with the nodes the plan holds. */
TEST(Medium_RunObserver, SeesEachBlockingRunOnce)
{
    ObservedPipeline pipeline;
    const auto observer = std::make_shared<RecordingObserver>();
    const RunObserverHandle handle = pipeline.runtime().add_run_observer(observer);

    for (int run = 0; run < 3; ++run)
        ASSERT_TRUE(pipeline.graph().run().has_value());

    ASSERT_EQ(observer->count(), 3u);
    for (const RunObservation& observation : observer->runs())
    {
        ASSERT_NE(observation.table, nullptr);
        EXPECT_EQ(observation.table->size(), 3u);
        EXPECT_EQ(observation.events.size(), 3u) << "one record per node dispatch";
        EXPECT_FALSE(observation.drops.any());
        for (const NodeEventRecord& record : observation.events)
        {
            EXPECT_LE(record.submit_ns, record.start_ns);
            EXPECT_LE(record.start_ns, record.end_ns);
        }
    }
}

/** @brief Tests that two observers both receive every run. */
TEST(Medium_RunObserver, TwoObserversBothReceiveEveryRun)
{
    ObservedPipeline pipeline;
    const auto first = std::make_shared<RecordingObserver>();
    const auto second = std::make_shared<RecordingObserver>();
    const RunObserverHandle first_handle = pipeline.runtime().add_run_observer(first);
    const RunObserverHandle second_handle = pipeline.runtime().add_run_observer(second);

    ASSERT_TRUE(pipeline.graph().run().has_value());
    ASSERT_TRUE(pipeline.graph().run().has_value());

    EXPECT_EQ(first->count(), 2u);
    EXPECT_EQ(second->count(), 2u);
    EXPECT_EQ(first->last().events.size(), second->last().events.size());
}

/** @brief Tests that removing one observer leaves the other receiving runs. */
TEST(Medium_RunObserver, RemovingOneLeavesTheOther)
{
    ObservedPipeline pipeline;
    const auto first = std::make_shared<RecordingObserver>();
    const auto second = std::make_shared<RecordingObserver>();
    RunObserverHandle first_handle = pipeline.runtime().add_run_observer(first);
    const RunObserverHandle second_handle = pipeline.runtime().add_run_observer(second);
    ASSERT_TRUE(pipeline.graph().run().has_value());

    first_handle.reset();
    ASSERT_TRUE(pipeline.graph().run().has_value());

    EXPECT_EQ(first->count(), 1u) << "a removed observer hears no later run";
    EXPECT_EQ(second->count(), 2u);
    EXPECT_FALSE(second->last().events.empty()) << "device timing stays on while one observer remains";
}

/** @brief Tests that no observer is called once its handle is reset. */
TEST(Medium_RunObserver, IsNotCalledAfterItsHandleIsReset)
{
    ObservedPipeline pipeline;
    const auto observer = std::make_shared<RecordingObserver>();
    RunObserverHandle handle = pipeline.runtime().add_run_observer(observer);
    ASSERT_TRUE(pipeline.graph().run().has_value());

    handle.reset();
    ASSERT_TRUE(pipeline.graph().run().has_value());
    ASSERT_TRUE(pipeline.graph().run().has_value());

    EXPECT_EQ(observer->count(), 1u);
}

/** @brief Tests that a handle that goes out of scope removes its observer. */
TEST(Medium_RunObserver, ADestroyedHandleRemovesItsObserver)
{
    ObservedPipeline pipeline;
    const auto observer = std::make_shared<RecordingObserver>();
    {
        const RunObserverHandle handle = pipeline.runtime().add_run_observer(observer);
        ASSERT_TRUE(pipeline.graph().run().has_value());
    }

    ASSERT_TRUE(pipeline.graph().run().has_value());

    EXPECT_EQ(observer->count(), 1u);
}

/** @brief Tests that a handle that outlives its runtime removes nothing and does not crash. */
TEST(Medium_RunObserver, AHandleThatOutlivesItsRuntimeRemovesNothing)
{
    RunObserverHandle handle;
    {
        ObservedPipeline pipeline;
        handle = pipeline.runtime().add_run_observer(std::make_shared<RecordingObserver>());
        ASSERT_TRUE(pipeline.graph().run().has_value());
    }

    EXPECT_NO_FATAL_FAILURE(handle.reset());
}

/** @brief Tests that an observer of one runtime is not called by the runs of another. */
TEST(Medium_RunObserver, AnObserverHearsOnlyItsOwnRuntime)
{
    ObservedPipeline observed;
    ObservedPipeline other;
    const auto observer = std::make_shared<RecordingObserver>();
    const RunObserverHandle handle = observed.runtime().add_run_observer(observer);

    ASSERT_TRUE(other.graph().run().has_value());
    ASSERT_TRUE(observed.graph().run().has_value());

    EXPECT_EQ(observer->count(), 1u);
}

/** @brief Tests that an async step is observed when its device work finishes, not when it is waited for. */
TEST(Medium_RunObserver, SeesAnAsyncStepWhenItsWorkFinishesNotWhenItIsWaitedFor)
{
    ObservedPipeline pipeline;
    const auto observer = std::make_shared<RecordingObserver>();
    const RunObserverHandle handle = pipeline.runtime().add_run_observer(observer);

    auto step = pipeline.graph().run_async().value();
    // The caller does not wait: the observation must arrive from the thread that learns the work is done.
    ASSERT_TRUE(wait_for_runs(*observer, 1)) << "the step's observation never arrived before wait()";
    EXPECT_EQ(observer->count(), 1u);
    EXPECT_EQ(observer->last().layer, ExecutionLayer::Scheduler);
    EXPECT_EQ(observer->last().events.size(), 3u);

    ASSERT_TRUE(step.wait().has_value());
    EXPECT_EQ(observer->count(), 1u) << "waiting for the step observes it no second time";
}

/** @brief Tests that an async step of a runtime with no observer reports to nobody and still completes. */
TEST(Medium_RunObserver, AnAsyncStepWithNoObserverStillCompletes)
{
    ObservedPipeline pipeline;

    auto step = pipeline.graph().run_async().value();

    ASSERT_TRUE(step.wait().has_value());
    EXPECT_EQ(pipeline.result()[10], 21.0f);
}

/** @brief Tests that a plan built before an observer was added reports with no records until it is rebuilt. */
TEST(Medium_RunObserver, SeesARunOfAPlanBuiltBeforeTheAddWithNoRecords)
{
    ObservedPipeline pipeline;
    const auto observer = std::make_shared<RecordingObserver>();
    RunObserverHandle handle;
    // A node of the run adds the observer, so that run's plan was built without device timing.
    pipeline.graph().add_host(Reads{}, Writes{}, [&pipeline, &handle, &observer]()
    {
        if (!handle)
            handle = pipeline.runtime().add_run_observer(observer);
    });

    ASSERT_TRUE(pipeline.graph().run().has_value());
    ASSERT_EQ(pipeline.graph().compile_count(), 1u);

    ASSERT_EQ(observer->count(), 1u) << "a run in flight when an observer is added reports to it";
    const RunObservation first = observer->last();
    EXPECT_TRUE(first.events.empty()) << "its plan recorded no device timing";
    ASSERT_NE(first.table, nullptr) << "the table of the plan is still handed over";

    ASSERT_TRUE(pipeline.graph().run().has_value());
    EXPECT_EQ(pipeline.graph().compile_count(), 2u) << "the plan is rebuilt once, with timing on";
    EXPECT_FALSE(observer->last().events.empty()) << "the next run uses the rebuilt plan";
}

/** @brief Tests that the first observer added costs exactly one plan rebuild. */
TEST(Medium_RunObserver, TheFirstAddCostsExactlyOneRebuild)
{
    ObservedPipeline pipeline;
    ASSERT_TRUE(pipeline.graph().run().has_value());
    ASSERT_EQ(pipeline.graph().compile_count(), 1u);

    const auto first = std::make_shared<RecordingObserver>();
    const RunObserverHandle first_handle = pipeline.runtime().add_run_observer(first);
    ASSERT_TRUE(pipeline.graph().run().has_value());
    EXPECT_EQ(pipeline.graph().compile_count(), 2u) << "adding the first observer rebuilds the plan once";

    const auto second = std::make_shared<RecordingObserver>();
    const RunObserverHandle second_handle = pipeline.runtime().add_run_observer(second);
    ASSERT_TRUE(pipeline.graph().run().has_value());
    ASSERT_TRUE(pipeline.graph().run().has_value());
    EXPECT_EQ(pipeline.graph().compile_count(), 2u) << "a second observer asks for nothing more";
}

/** @brief Tests that removing the last observer costs exactly one plan rebuild and removing another costs none. */
TEST(Medium_RunObserver, TheLastRemoveCostsExactlyOneRebuild)
{
    ObservedPipeline pipeline;
    RunObserverHandle first = pipeline.runtime().add_run_observer(std::make_shared<RecordingObserver>());
    RunObserverHandle second = pipeline.runtime().add_run_observer(std::make_shared<RecordingObserver>());
    ASSERT_TRUE(pipeline.graph().run().has_value());
    ASSERT_EQ(pipeline.graph().compile_count(), 1u);

    first.reset();
    ASSERT_TRUE(pipeline.graph().run().has_value());
    EXPECT_EQ(pipeline.graph().compile_count(), 1u) << "one observer remains, so timing stays on";

    second.reset();
    ASSERT_TRUE(pipeline.graph().run().has_value());
    EXPECT_EQ(pipeline.graph().compile_count(), 2u) << "the last remove turns timing off, once";
    ASSERT_TRUE(pipeline.graph().run().has_value());
    EXPECT_EQ(pipeline.graph().compile_count(), 2u);
}

/** @brief Tests that a graph run with no observer attached records no device timing and reports to nobody. */
TEST(Medium_RunObserver, ARunWithNoObserverMakesNoObservation)
{
    ObservedPipeline pipeline;

    ASSERT_TRUE(pipeline.graph().run().has_value());

    EXPECT_EQ(pipeline.graph().compile_count(), 1u);
    EXPECT_EQ(pipeline.result()[3], 7.0f) << "the run itself is unchanged by there being no observer";
}

/** @brief Tests that a run on the chain path is observed with the chain layer and its records. */
TEST(Medium_RunObserver, ARunOnTheChainPathCarriesTheChainLayer)
{
    ObservedPipeline pipeline(ObservedPipeline::chain_config());
    const auto observer = std::make_shared<RecordingObserver>();
    const RunObserverHandle handle = pipeline.runtime().add_run_observer(observer);

    ASSERT_TRUE(pipeline.graph().run().has_value());

    ASSERT_EQ(observer->count(), 1u);
    const RunObservation observation = observer->last();
    if (observation.layer != ExecutionLayer::Chain)
        GTEST_SKIP() << "this device's plan does not classify as a chain";
    EXPECT_EQ(observation.events.size(), 3u);
}

/** @brief Tests that a multi-step run is one observation holding every step's records. */
TEST(Medium_RunObserver, AMultiStepRunIsOneObservationWithEveryStep)
{
    ObservedPipeline pipeline;
    const auto observer = std::make_shared<RecordingObserver>();
    const RunObserverHandle handle = pipeline.runtime().add_run_observer(observer);

    ASSERT_TRUE(pipeline.graph().run(4).has_value());

    ASSERT_EQ(observer->count(), 1u);
    const RunObservation observation = observer->last();
    EXPECT_EQ(observation.events.size(), 12u) << "three nodes over four steps";
    std::uint32_t highest = 0;
    for (const NodeEventRecord& record : observation.events)
        highest = std::max(highest, record.invocation);
    EXPECT_EQ(highest, 3u);
}
