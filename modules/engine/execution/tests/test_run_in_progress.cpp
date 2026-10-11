/**************************************************************************/
/* test_run_in_progress.cpp                                               */
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
 * @file test_run_in_progress.cpp
 * @brief Tests when ExecutionEngine::run_in_progress() reports a run and when it stops.
 * @see modules/engine/execution/tests/README.md
 * @author Mustafa Garip
 */

#include <atomic>
#include <cstddef>
#include <future>
#include <stdexcept>
#include <vector>
#include <gtest/gtest.h>
#include <sycl/sycl.hpp>
#include <SushiRuntime/memory/resource_region.hpp>
#include <SushiRuntime/memory/usm_request.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/task_types.hpp>
#include <SushiRuntime/graph/compiled_graph.hpp>
#include <SushiRuntime/graph/task_graph.hpp>
#include <SushiRuntime/execution/chain_plan.hpp>
#include <SushiRuntime/execution/chain_run.hpp>
#include <SushiRuntime/execution/execution_engine.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>

namespace
{
    using SushiRuntime::Internal::Execution::ChainRun;
    using SushiRuntime::Internal::Execution::ExecutionEngine;
    using SushiRuntime::Internal::Execution::RuntimeContext;
    using SushiRuntime::Internal::Graph::CompiledGraph;
    using SushiRuntime::Internal::Graph::TaskGraph;
    using SushiRuntime::Internal::Graph::TaskMetadata;

    /** @brief Implements only the pure members, so every query keeps its default. */
    class MinimalEngine final : public ExecutionEngine
    {
        public:
            /** @brief Runs nothing. */
            void execute(CompiledGraph&) override
            {

            }

            /** @brief Stops nothing. */
            void shutdown() override
            {

            }
    };

    /** @brief Compiles a graph of one host task that runs @p work. */
    template <class Work>
    CompiledGraph compile_host_task(Work work)
    {
        TaskGraph graph;
        graph.add_task(TaskMetadata{"probe"}, {}, {},
                       [work](sycl::queue&, const std::vector<sycl::event>&) -> sycl::event
                       {
                           work();
                           return {};
                       });
        return graph.compile();
    }

    /** @brief Owns one float array of shared USM on device 0 of a context. */
    class DeviceFloats final
    {
        public:
            /** @brief Allocates @p count floats through @p context, which outlives this object. */
            DeviceFloats(RuntimeContext& context, std::size_t count)
                : context_(context),
                  data_(static_cast<float*>(context.memory().allocate(
                      {SushiRuntime::Internal::Memory::USMKind::Shared, count * sizeof(float), 0, 0})))
            {

            }

            /** @brief Releases the array. */
            ~DeviceFloats()
            {
                if (data_ != nullptr)
                    context_.memory().release(data_);
            }

            DeviceFloats(const DeviceFloats&) = delete;
            DeviceFloats& operator=(const DeviceFloats&) = delete;
            DeviceFloats(DeviceFloats&&) = delete;
            DeviceFloats& operator=(DeviceFloats&&) = delete;

            /** @brief Returns the array, or nullptr when the allocation failed. */
            [[nodiscard]] float* data() const noexcept
            {
                return data_;
            }

        private:
            RuntimeContext& context_; /**< The context that allocated the array. */
            float* data_;             /**< The array. */
    };

    /** @brief Compiles and classifies a plan of one kernel that fills @p data with ones. */
    CompiledGraph compile_chain_candidate(RuntimeContext& context, float* data, std::size_t count)
    {
        const std::vector<SushiRuntime::Internal::Memory::ResourceRegion> none;
        const std::vector<SushiRuntime::Internal::Memory::ResourceRegion> writes{
            SushiRuntime::Internal::Memory::ResourceRegion(data)};

        TaskGraph graph;
        graph.add_node([data, count](sycl::handler& handler)
        {
            handler.parallel_for(sycl::range<1>(count), [=](sycl::id<1> index)
            {
                data[index] = 1.0f;
            });
        }, none, writes);

        CompiledGraph plan = graph.compile();
        plan.capture_base_state();
        plan.chain = SushiRuntime::Internal::Execution::classify_chain(plan, context);
        return plan;
    }
} // namespace

TEST(Unit_RunInProgress, TheInterfaceDefaultReportsNoRun)
{
    const MinimalEngine engine;
    EXPECT_FALSE(engine.run_in_progress());
}

TEST(Integration_RunInProgress, AFreshEngineReportsNoRun)
{
    RuntimeContext context;
    EXPECT_FALSE(context.execution_engine().run_in_progress());
}

TEST(Integration_RunInProgress, ABlockingRunIsInProgressUntilExecuteReturns)
{
    RuntimeContext context;
    ExecutionEngine& engine = context.execution_engine();

    std::atomic<bool> seen_inside{false};
    CompiledGraph plan = compile_host_task([&engine, &seen_inside]
    {
        seen_inside.store(engine.run_in_progress(), std::memory_order_relaxed);
    });

    engine.execute(plan);

    EXPECT_TRUE(seen_inside.load(std::memory_order_relaxed))
        << "a host task of the run saw no run in progress";
    EXPECT_FALSE(engine.run_in_progress());
}

TEST(Integration_RunInProgress, AnAsynchronousRunIsInProgressUntilCompleteReturns)
{
    RuntimeContext context;
    ExecutionEngine& engine = context.execution_engine();
    ASSERT_TRUE(engine.supports_asynchronous_run());

    // The task waits on the gate, so the run cannot finish before the first check.
    std::promise<void> gate;
    const std::shared_future<void> opened = gate.get_future().share();
    CompiledGraph plan = compile_host_task([opened] { opened.wait(); });

    engine.submit(plan);
    EXPECT_TRUE(engine.run_in_progress());

    gate.set_value();
    engine.complete();
    EXPECT_FALSE(engine.run_in_progress());
}

TEST(Integration_RunInProgress, ARunWhoseTaskThrowsLeavesNoRunInProgress)
{
    RuntimeContext context;
    ExecutionEngine& engine = context.execution_engine();

    CompiledGraph blocking = compile_host_task([] { throw std::runtime_error("probe failure"); });
    EXPECT_THROW(engine.execute(blocking), std::runtime_error);
    EXPECT_FALSE(engine.run_in_progress());

    CompiledGraph asynchronous =
        compile_host_task([] { throw std::runtime_error("probe failure"); });
    engine.submit(asynchronous);
    EXPECT_TRUE(engine.run_in_progress());
    EXPECT_THROW(engine.complete(), std::runtime_error);
    EXPECT_FALSE(engine.run_in_progress());
}

TEST(Integration_RunInProgress, AChainedRunIsInProgressUntilFinishReturnsOrItIsDestroyed)
{
    constexpr std::size_t COUNT = 64;

    RuntimeContext context;
    ExecutionEngine& engine = context.execution_engine();
    const DeviceFloats floats(context, COUNT);
    ASSERT_NE(floats.data(), nullptr);

    CompiledGraph plan = compile_chain_candidate(context, floats.data(), COUNT);
    if (!plan.chain.chainable)
        GTEST_SKIP() << "classify_chain() left the one-kernel plan on the scheduler.";

    {
        ChainRun finished(context, plan);
        EXPECT_TRUE(engine.run_in_progress());
        finished.submit_step(0);
        EXPECT_TRUE(engine.run_in_progress());
        finished.finish();
        EXPECT_FALSE(engine.run_in_progress()) << "finish() returned and the run still shows";
    }
    EXPECT_FALSE(engine.run_in_progress());
    EXPECT_EQ(floats.data()[COUNT - 1], 1.0f);

    {
        const ChainRun abandoned(context, plan);
        EXPECT_TRUE(engine.run_in_progress());
    }
    EXPECT_FALSE(engine.run_in_progress()) << "the destructor left the run showing";
}

TEST(Integration_RunInProgress, AScheduledRunInsideAChainedRunLeavesTheChainedRunShowing)
{
    constexpr std::size_t COUNT = 64;

    RuntimeContext context;
    ExecutionEngine& engine = context.execution_engine();
    const DeviceFloats floats(context, COUNT);
    ASSERT_NE(floats.data(), nullptr);

    CompiledGraph chained = compile_chain_candidate(context, floats.data(), COUNT);
    if (!chained.chain.chainable)
        GTEST_SKIP() << "classify_chain() left the one-kernel plan on the scheduler.";

    CompiledGraph scheduled = compile_host_task([] {});
    {
        ChainRun outer(context, chained);
        engine.execute(scheduled);
        EXPECT_TRUE(engine.run_in_progress())
            << "the scheduled run's end cleared the chained run's mark";

        const ChainRun inner(context, chained);
        outer.finish();
        EXPECT_TRUE(engine.run_in_progress()) << "one chained run's end cleared the other's";
    }
    EXPECT_FALSE(engine.run_in_progress());
}
