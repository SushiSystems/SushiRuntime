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
#include <atomic>
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

/** @brief Tests that the first run after an add carries full records even though its plan was built before the add. */
TEST(Medium_RunObserver, TheFirstRunAfterAnAddOfAPlanBuiltBeforeCarriesRecords)
{
    ObservedPipeline pipeline;
    ASSERT_TRUE(pipeline.graph().run().has_value());
    ASSERT_EQ(pipeline.graph().compile_count(), 1u) << "built with no observer added";

    const auto observer = std::make_shared<RecordingObserver>();
    const RunObserverHandle handle = pipeline.runtime().add_run_observer(observer);
    ASSERT_TRUE(pipeline.graph().run().has_value());

    ASSERT_EQ(observer->count(), 1u);
    const RunObservation observation = observer->last();
    EXPECT_EQ(observation.events.size(), 3u) << "each run chooses its timing when it is armed, so this one is timed";
    ASSERT_NE(observation.table, nullptr);
    EXPECT_EQ(observation.table->size(), 3u);
    EXPECT_EQ(pipeline.graph().compile_count(), 1u) << "the plan was not rebuilt for it";
}

/** @brief Tests that adding an observer costs no plan rebuild, for the first observer and for later ones. */
TEST(Medium_RunObserver, AddingAnObserverCostsNoRebuild)
{
    ObservedPipeline pipeline;
    ASSERT_TRUE(pipeline.graph().run().has_value());
    ASSERT_EQ(pipeline.graph().compile_count(), 1u);

    const auto first = std::make_shared<RecordingObserver>();
    const RunObserverHandle first_handle = pipeline.runtime().add_run_observer(first);
    ASSERT_TRUE(pipeline.graph().run().has_value());
    EXPECT_EQ(pipeline.graph().compile_count(), 1u) << "adding the first observer rebuilds nothing";

    const auto second = std::make_shared<RecordingObserver>();
    const RunObserverHandle second_handle = pipeline.runtime().add_run_observer(second);
    ASSERT_TRUE(pipeline.graph().run().has_value());
    ASSERT_TRUE(pipeline.graph().run().has_value());
    EXPECT_EQ(pipeline.graph().compile_count(), 1u) << "nor does a second one";
    EXPECT_EQ(second->count(), 2u);
}

/** @brief Tests that removing the last observer costs no plan rebuild and the runs after it are untimed. */
TEST(Medium_RunObserver, RemovingTheLastObserverCostsNoRebuild)
{
    ObservedPipeline pipeline;
    RunObserverHandle first = pipeline.runtime().add_run_observer(std::make_shared<RecordingObserver>());
    const auto last_observer = std::make_shared<RecordingObserver>();
    RunObserverHandle second = pipeline.runtime().add_run_observer(last_observer);
    ASSERT_TRUE(pipeline.graph().run().has_value());
    ASSERT_EQ(pipeline.graph().compile_count(), 1u);

    first.reset();
    ASSERT_TRUE(pipeline.graph().run().has_value());
    EXPECT_EQ(pipeline.graph().compile_count(), 1u);
    EXPECT_FALSE(last_observer->last().events.empty()) << "one observer remains, so the run is still timed";

    second.reset();
    ASSERT_TRUE(pipeline.graph().run().has_value());
    ASSERT_TRUE(pipeline.graph().run().has_value());
    EXPECT_EQ(pipeline.graph().compile_count(), 1u) << "the last remove rebuilds nothing either";
    EXPECT_EQ(last_observer->count(), 2u) << "and the runs after it reach no observer";
}

/** @brief Tests that an observer added while a step is in flight gets that step with no records and times the next run. */
TEST(Medium_RunObserver, AnObserverAddedMidRunTimesTheNextRunNotTheCurrentOne)
{
    ObservedPipeline pipeline;
    std::atomic<bool> gate{false};
    pipeline.graph().add_host(Reads{}, Writes{}, [&gate]()
    {
        const auto deadline = std::chrono::steady_clock::now() + K_DELIVERY_TIMEOUT;
        while (!gate.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
    });
    auto step = pipeline.graph().run_async().value();

    // The step was armed with no observer, so it is untimed; the observer joins while it is held open.
    const auto observer = std::make_shared<RecordingObserver>();
    const RunObserverHandle handle = pipeline.runtime().add_run_observer(observer);
    gate.store(true, std::memory_order_release);
    ASSERT_TRUE(step.wait().has_value());

    ASSERT_TRUE(wait_for_runs(*observer, 1));
    const RunObservation in_flight = observer->last();
    EXPECT_TRUE(in_flight.events.empty()) << "the run in flight chose its timing before the observer was added";
    EXPECT_NE(in_flight.table, nullptr) << "the table of its plan is handed over all the same";

    gate.store(true, std::memory_order_release);
    ASSERT_TRUE(pipeline.graph().run().has_value());
    ASSERT_EQ(observer->count(), 2u);
    EXPECT_EQ(observer->last().events.size(), 4u) << "the next run is timed: three kernels and the host node";
    EXPECT_EQ(pipeline.graph().compile_count(), 1u);
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
