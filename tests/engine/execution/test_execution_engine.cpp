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

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/core/clock.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/seeded_executor.hpp>
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

#include "support/host_context.hpp"
#include "support/host_runtime.hpp"

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

            /** @brief Starts nothing. */
            SushiRuntime::Result<void> submit(CompiledGraph&) override
            {
                return {};
            }

            /** @brief Waits for nothing. */
            SushiRuntime::Result<void> complete() override
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

    /** @brief Compiles a graph of one host node that runs @p work, which has a host form. */
    template <class Work>
    CompiledGraph compile_native_task(Work work)
    {
        TaskGraph graph;
        graph.add_native({}, {}, work);
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

/** @brief Tests that a blocking run is in progress for a node of the run, and not after execute() returns. */
TEST(Small_RunInProgress, ABlockingRunIsInProgressUntilExecuteReturns)
{
    const auto context_owner = TestSupport::make_host_context();
    ExecutionEngine& engine = context_owner->execution_engine();

    bool seen_inside = false;
    CompiledGraph plan = compile_native_task([&engine, &seen_inside]
    {
        seen_inside = engine.run_in_progress();
    });

    engine.execute(plan).value();

    EXPECT_TRUE(seen_inside) << "a host task of the run saw no run in progress";
    EXPECT_FALSE(engine.run_in_progress());
}

/** @brief Tests that a submitted run is in progress until complete() returns. */
TEST(Small_RunInProgress, AnAsynchronousRunIsInProgressUntilCompleteReturns)
{
    const auto context_owner = TestSupport::make_host_context();
    ExecutionEngine& engine = context_owner->execution_engine();
    ASSERT_TRUE(engine.supports_asynchronous_run());
    CompiledGraph plan = compile_native_task([] {});

    engine.submit(plan).value();
    EXPECT_TRUE(engine.run_in_progress());

    engine.complete().value();
    EXPECT_FALSE(engine.run_in_progress());
}

/** @brief Tests that a blocking run whose task throws returns KernelFailed and leaves no run in progress. */
TEST(Small_RunInProgress, ABlockingRunWhoseTaskThrowsReturnsKernelFailed)
{
    const auto context_owner = TestSupport::make_host_context();
    ExecutionEngine& engine = context_owner->execution_engine();

    CompiledGraph blocking = compile_native_task([] { throw std::runtime_error("probe failure"); });
    const SushiRuntime::Result<void> blocked = engine.execute(blocking);

    ASSERT_FALSE(blocked.has_value());
    EXPECT_EQ(blocked.error().code, SushiRuntime::ErrorCode::KernelFailed);
    EXPECT_NE(blocked.error().message.find("probe failure"), std::string::npos)
        << blocked.error().message;
    EXPECT_FALSE(engine.run_in_progress());
}

/** @brief Tests that an asynchronous run whose task throws returns KernelFailed from complete(). */
TEST(Small_RunInProgress, AnAsynchronousRunWhoseTaskThrowsReturnsKernelFailedFromComplete)
{
    const auto context_owner = TestSupport::make_host_context();
    ExecutionEngine& engine = context_owner->execution_engine();
    CompiledGraph asynchronous =
        compile_native_task([] { throw std::runtime_error("probe failure"); });
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

/** @brief Tests the engine of a host context: order, seeding, asynchronous runs, failure and cancel. */
namespace HostEngineTests
{
    using SushiRuntime::BufferRegion;
    using SushiRuntime::ErrorCode;
    using SushiRuntime::SeededExecutor;
    using SushiRuntime::Internal::Execution::ExecutionEngine;
    using SushiRuntime::Internal::Execution::RuntimeContext;
    using SushiRuntime::Internal::Graph::CompiledGraph;
    using SushiRuntime::Internal::Graph::Node;
    using SushiRuntime::Internal::Graph::TaskGraph;

    /** @brief Holds the engine of a host context, with the executor the context was made on. */
    struct HostEngine
    {
        /** @brief Makes a host context whose executor draws from a seed. */
        explicit HostEngine(std::uint64_t seed = 0)
            : executor(std::make_shared<SeededExecutor>(seed)),
              context(TestSupport::make_host_context(executor)),
              engine(context->execution_engine())
        {
        }

        std::shared_ptr<SeededExecutor> executor; /**< Runs the engine's nodes. */
        std::shared_ptr<RuntimeContext> context;  /**< Owns the engine. */
        ExecutionEngine& engine;                  /**< The engine under test. */
    };

    /**
     * @brief Builds a diamond of host nodes over four cells: a, then b and c from a, then d from b and c.
     * @param order Gains each node's letter as it runs.
     */
    CompiledGraph diamond(std::vector<char>& order, int* cells)
    {
        TaskGraph graph;
        graph.add_native({}, {BufferRegion(&cells[0])}, [&order]() { order.push_back('a'); });
        graph.add_native({BufferRegion(&cells[0])}, {BufferRegion(&cells[1])},
                         [&order]() { order.push_back('b'); });
        graph.add_native({BufferRegion(&cells[0])}, {BufferRegion(&cells[2])},
                         [&order]() { order.push_back('c'); });
        graph.add_native({BufferRegion(&cells[1]), BufferRegion(&cells[2])},
                         {BufferRegion(&cells[3])}, [&order]() { order.push_back('d'); });
        return graph.compile().value();
    }

    /** @brief Builds a graph of independent host nodes, each appending its number to order. */
    CompiledGraph siblings(std::vector<int>& order, int count)
    {
        TaskGraph graph;
        for (int node = 0; node < count; ++node)
            graph.add_native({}, {}, [&order, node]() { order.push_back(node); });
        return graph.compile().value();
    }

    /** @brief Tests that a diamond runs its first node first and its last node last. */
    TEST(Small_HostExecutionEngine, RunsADiamondInDependencyOrder)
    {
    HostEngine host;
    std::vector<char> order;
    int cells[4] = {};
    CompiledGraph plan = diamond(order, cells);

    ASSERT_TRUE(host.engine.execute(plan).has_value());

    ASSERT_EQ(order.size(), 4u);
    EXPECT_EQ(order.front(), 'a');
    EXPECT_EQ(order.back(), 'd');
    }

    /** @brief Tests that two engines of one seed run unordered nodes in the same order. */
    TEST(Small_HostExecutionEngine, OneSeedReplaysTheOrderOfUnorderedNodes)
    {
    HostEngine first(11);
    HostEngine second(11);
    std::vector<int> first_order;
    std::vector<int> second_order;
    CompiledGraph first_plan = siblings(first_order, 8);
    CompiledGraph second_plan = siblings(second_order, 8);

    ASSERT_TRUE(first.engine.execute(first_plan).has_value());
    ASSERT_TRUE(second.engine.execute(second_plan).has_value());

    EXPECT_EQ(first_order, second_order);
    }

    /** @brief Tests that submit runs nothing until the executor is stepped. */
    TEST(Small_HostExecutionEngine, SubmitRunsNothingUntilTheExecutorIsStepped)
    {
    HostEngine host;
    std::vector<int> order;
    CompiledGraph plan = siblings(order, 3);

    ASSERT_TRUE(host.engine.submit(plan).has_value());

    EXPECT_TRUE(order.empty());
    EXPECT_FALSE(host.engine.run_finished());
    }

    /** @brief Tests that complete drains the executor and finishes the run it submitted. */
    TEST(Small_HostExecutionEngine, CompleteFinishesTheSubmittedRun)
    {
    HostEngine host;
    std::vector<int> order;
    CompiledGraph plan = siblings(order, 3);
    ASSERT_TRUE(host.engine.submit(plan).has_value());

    ASSERT_TRUE(host.engine.complete().has_value());

    EXPECT_EQ(order.size(), 3u);
    EXPECT_TRUE(host.engine.run_finished());
    }

    /** @brief Tests that a second submit during a run is refused with InvalidState. */
    TEST(Small_HostExecutionEngine, RefusesASubmitDuringARun)
    {
    HostEngine host;
    std::vector<int> order;
    CompiledGraph plan = siblings(order, 2);
    ASSERT_TRUE(host.engine.submit(plan).has_value());

    auto refused = host.engine.submit(plan);

    ASSERT_FALSE(refused.has_value());
    EXPECT_EQ(refused.error().code, ErrorCode::InvalidState);
    }

    /** @brief Tests that a node that throws fails the run and its successor does not run. */
    TEST(Small_HostExecutionEngine, AThrowingNodeFailsTheRunAndSkipsItsSuccessor)
    {
    HostEngine host;
    int cell = 0;
    bool successor_ran = false;
    TaskGraph graph;
    graph.add_native({}, {BufferRegion(&cell)}, []() { throw std::runtime_error("boom"); });
    graph.add_native({BufferRegion(&cell)}, {}, [&successor_ran]() { successor_ran = true; });
    CompiledGraph plan = graph.compile().value();

    auto ran = host.engine.execute(plan);

    ASSERT_FALSE(ran.has_value());
    EXPECT_FALSE(successor_ran);
    }

    /** @brief Tests that a command group with no host form fails the run with Unsupported. */
    TEST(Small_HostExecutionEngine, ACommandGroupWithNoHostFormFailsWithUnsupported)
    {
    HostEngine host;
    TaskGraph graph;
    graph.add_node([](sycl::handler&) {}, std::vector<sycl::event>{});
    CompiledGraph plan = graph.compile().value();

    auto ran = host.engine.execute(plan);

    ASSERT_FALSE(ran.has_value());
    EXPECT_EQ(ran.error().code, ErrorCode::Unsupported);
    }

    /** @brief Tests that a command group with a host form runs that form in its place. */
    TEST(Small_HostExecutionEngine, RunsTheHostFormOfACommandGroup)
    {
    HostEngine host;
    int value = 0;
    TaskGraph graph;
    Node& node = graph.add_node([](sycl::handler&) {}, std::vector<sycl::event>{});
    node.host_form = [&value]() { value = 7; };
    CompiledGraph plan = graph.compile().value();

    ASSERT_TRUE(host.engine.execute(plan).has_value());

    EXPECT_EQ(value, 7);
    }

    /** @brief Tests that a cancelled run skips the nodes it had not run. */
    TEST(Small_HostExecutionEngine, ACancelledRunSkipsTheNodesNotYetRun)
    {
    HostEngine host;
    std::vector<int> order;
    CompiledGraph plan = siblings(order, 4);
    ASSERT_TRUE(host.engine.submit(plan).has_value());

    host.engine.cancel();
    static_cast<void>(host.engine.complete());

    EXPECT_TRUE(order.empty());
    }
} // namespace HostEngineTests
