/**************************************************************************/
/* test_execution_engine.cpp                                              */
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
 * @file test_execution_engine.cpp
 * @brief Tests when an execution engine reports a run in progress.
 * @see tests/engine/execution/README.md
 * @author Mustafa Garip
 */

#include <atomic>
#include <cstddef>
#include <future>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/region/buffer_region.hpp>
#include <SushiRuntime/memory/usm_request.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/task_types.hpp>
#include <SushiRuntime/graph/compiled_graph.hpp>
#include <SushiRuntime/graph/task_graph.hpp>
#include <SushiRuntime/execution/chain_plan.hpp>
#include <SushiRuntime/execution/chain_run.hpp>
#include <SushiRuntime/execution/execution_engine.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/runtime_assembly/context_factory.hpp>

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
            SushiRuntime::Result<void> execute(CompiledGraph&) override
            {
                return {};
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
        return graph.compile().value();
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
        const std::vector<SushiRuntime::BufferRegion> none;
        const std::vector<SushiRuntime::BufferRegion> writes{
            SushiRuntime::BufferRegion(data)};

        TaskGraph graph;
        graph.add_node([data, count](sycl::handler& handler)
        {
            handler.parallel_for(sycl::range<1>(count), [=](sycl::id<1> index)
            {
                data[index] = 1.0f;
            });
        }, none, writes);

        CompiledGraph plan = graph.compile().value();
        plan.capture_base_state();
        plan.chain = SushiRuntime::Internal::Execution::classify_chain(plan, context);
        return plan;
    }
} // namespace

/** @brief Tests that an engine that implements only the pure members reports no run in progress. */
TEST(Small_RunInProgress, TheInterfaceDefaultReportsNoRun)
{
    const MinimalEngine engine;
    EXPECT_FALSE(engine.run_in_progress());
}

TEST(Medium_RunInProgress, ABlockingRunIsInProgressUntilExecuteReturns)
{
    const auto context_owner = SushiRuntime::Test::make_context();
    RuntimeContext& context = *context_owner;
    ExecutionEngine& engine = context.execution_engine();

    std::atomic<bool> seen_inside{false};
    CompiledGraph plan = compile_host_task([&engine, &seen_inside]
    {
        seen_inside.store(engine.run_in_progress(), std::memory_order_relaxed);
    });

    engine.execute(plan).value();

    EXPECT_TRUE(seen_inside.load(std::memory_order_relaxed))
        << "a host task of the run saw no run in progress";
    EXPECT_FALSE(engine.run_in_progress());
}

TEST(Medium_RunInProgress, AnAsynchronousRunIsInProgressUntilCompleteReturns)
{
    const auto context_owner = SushiRuntime::Test::make_context();
    RuntimeContext& context = *context_owner;
    ExecutionEngine& engine = context.execution_engine();
    ASSERT_TRUE(engine.supports_asynchronous_run());

    // The task waits on the gate, so the run cannot finish before the first check.
    std::promise<void> gate;
    const std::shared_future<void> opened = gate.get_future().share();
    CompiledGraph plan = compile_host_task([opened] { opened.wait(); });

    engine.submit(plan).value();
    EXPECT_TRUE(engine.run_in_progress());

    gate.set_value();
    engine.complete().value();
    EXPECT_FALSE(engine.run_in_progress());
}

/** @brief Tests that a blocking run whose task throws returns KernelFailed and leaves no run in progress. */
TEST(Medium_RunInProgress, ABlockingRunWhoseTaskThrowsReturnsKernelFailed)
{
    const auto context_owner = SushiRuntime::Test::make_context();
    RuntimeContext& context = *context_owner;
    ExecutionEngine& engine = context.execution_engine();

    CompiledGraph blocking = compile_host_task([] { throw std::runtime_error("probe failure"); });
    const SushiRuntime::Result<void> blocked = engine.execute(blocking);

    ASSERT_FALSE(blocked.has_value());
    EXPECT_EQ(blocked.error().code, SushiRuntime::ErrorCode::KernelFailed);
    EXPECT_NE(blocked.error().message.find("probe failure"), std::string::npos)
        << blocked.error().message;
    EXPECT_FALSE(engine.run_in_progress());
}

/** @brief Tests that an asynchronous run whose task throws returns KernelFailed from complete(). */
TEST(Medium_RunInProgress, AnAsynchronousRunWhoseTaskThrowsReturnsKernelFailedFromComplete)
{
    const auto context_owner = SushiRuntime::Test::make_context();
    RuntimeContext& context = *context_owner;
    ExecutionEngine& engine = context.execution_engine();
    CompiledGraph asynchronous =
        compile_host_task([] { throw std::runtime_error("probe failure"); });
    engine.submit(asynchronous).value();

    const SushiRuntime::Result<void> completed = engine.complete();

    ASSERT_FALSE(completed.has_value());
    EXPECT_EQ(completed.error().code, SushiRuntime::ErrorCode::KernelFailed);
    EXPECT_FALSE(engine.run_in_progress());
}

/** @brief Fixture of the tests that run a one-kernel plan the classification chains. */
class Medium_ChainedRunInProgress : public ::testing::Test
{
    protected:
        static constexpr std::size_t K_COUNT = 64;

        /** @brief Builds the context, the array and the chained plan; the plan must be chainable. */
        void SetUp() override
        {
            context_owner_ = SushiRuntime::Test::make_context();
            floats_.emplace(*context_owner_, K_COUNT);
            ASSERT_NE(floats_->data(), nullptr);
            plan_ = compile_chain_candidate(*context_owner_, floats_->data(), K_COUNT);
            ASSERT_TRUE(plan_.chain.chainable) << "classify_chain() left the one-kernel plan on the scheduler.";
        }

        /** @brief Returns the context of the test. */
        RuntimeContext& context() { return *context_owner_; }

        /** @brief Returns the engine of the context. */
        ExecutionEngine& engine() { return context_owner_->execution_engine(); }

        /** @brief Returns the array the plan fills. */
        float* data() { return floats_->data(); }

        /** @brief Returns the chained plan. */
        CompiledGraph& plan() { return plan_; }

    private:
        std::shared_ptr<RuntimeContext> context_owner_; /**< The context the array and the plan belong to. */
        std::optional<DeviceFloats> floats_;            /**< The array the plan's kernel fills. */
        CompiledGraph plan_;                            /**< The chained plan. */
};

/** @brief Tests that a chained run is in progress from its creation until finish() returns. */
TEST_F(Medium_ChainedRunInProgress, AChainedRunIsInProgressUntilFinishReturns)
{
    {
        ChainRun finished(context(), plan());
        EXPECT_TRUE(engine().run_in_progress());
        finished.submit_step(0).value();
        EXPECT_TRUE(engine().run_in_progress());
        finished.finish().value();
        EXPECT_FALSE(engine().run_in_progress()) << "finish() returned and the run still shows";
    }

    EXPECT_FALSE(engine().run_in_progress());
    EXPECT_EQ(data()[K_COUNT - 1], 1.0f);
}

/** @brief Tests that destroying a chained run that never finished clears the mark. */
TEST_F(Medium_ChainedRunInProgress, AnAbandonedChainedRunLeavesNothingShowing)
{
    {
        const ChainRun abandoned(context(), plan());
        EXPECT_TRUE(engine().run_in_progress());
    }

    EXPECT_FALSE(engine().run_in_progress()) << "the destructor left the run showing";
}

/** @brief Tests that a scheduled run inside a chained run leaves the chained run showing. */
TEST_F(Medium_ChainedRunInProgress, AScheduledRunInsideAChainedRunLeavesTheChainedRunShowing)
{
    CompiledGraph scheduled = compile_host_task([] {});
    {
        ChainRun outer(context(), plan());

        engine().execute(scheduled).value();

        EXPECT_TRUE(engine().run_in_progress())
            << "the scheduled run's end cleared the chained run's mark";
        outer.finish().value();
    }
    EXPECT_FALSE(engine().run_in_progress());
}

/** @brief Tests that the end of one chained run does not clear the mark of another that is still open. */
TEST_F(Medium_ChainedRunInProgress, OneChainedRunsEndLeavesTheOtherShowing)
{
    {
        ChainRun outer(context(), plan());
        const ChainRun inner(context(), plan());

        outer.finish().value();

        EXPECT_TRUE(engine().run_in_progress()) << "one chained run's end cleared the other's";
    }
    EXPECT_FALSE(engine().run_in_progress());
}
