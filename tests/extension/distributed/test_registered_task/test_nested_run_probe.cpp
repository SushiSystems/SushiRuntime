/**************************************************************************/
/* test_nested_run_probe.cpp                                              */
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
 * @file test_nested_run_probe.cpp
 * @brief Probes whether a registered kernel may run a compiled graph from inside its body.
 * @see tests/extension/distributed/README.md
 * @author Mustafa Garip
 */

#include <atomic>
#include <chrono>
#include <cstddef>
#include <future>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/api/fluent.hpp>
#include <SushiRuntime/api/graph/access.hpp>
#include <SushiRuntime/api/graph/node_options.hpp>
#include <SushiRuntime/api/graph/recorder.hpp>
#include <SushiRuntime/cluster_load/offload_policy.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/operation_id.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/device_probe/device_probe.hpp>
#include <SushiRuntime/distributed/buffer_view.hpp>
#include <SushiRuntime/distributed/cluster.hpp>
#include <SushiRuntime/distributed/in_process_transport.hpp>
#include <SushiRuntime/distributed/kernel_registry.hpp>
#include <SushiRuntime/distributed/registered_task.hpp>
#include <SushiRuntime/distributed/worker_agent.hpp>
#include <SushiRuntime/region/buffer_region.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;

namespace
{
    /// @brief Names the operation whose kernel runs the inner graph.
    const OperationID K_NESTED_RUN(0x4e52);

    /// @brief Holds how long a test waits for a run before it fails.
    constexpr std::chrono::seconds K_WAIT{10};

    /// @brief Holds the element count of every buffer.
    constexpr std::size_t K_COUNT = 64;

    /// @brief Holds what an outcome holds before the kernel has run the inner graph.
    constexpr int K_PENDING = -1;

    /// @brief Holds what an outcome holds when the inner graph ran to its end.
    constexpr int K_COMPLETED = -2;

    /// @brief Holds the value an inner output element holds before the task wrote it.
    constexpr float K_UNWRITTEN = -1.0f;

    /**
     * @brief Holds how the kernel's run of the inner graph ended.
     *
     * It holds the ErrorCode of a refusal as an integer, K_COMPLETED for a run that returned a
     * report, and K_PENDING until the kernel has run.
     */
    class NestedOutcome final
    {
        public:
            /** @brief Stores how the inner run ended. */
            void record(const Result<RunReport, Error>& ran)
            {
                state_.store(ran.has_value() ? K_COMPLETED : static_cast<int>(ran.error().code),
                             std::memory_order_release);
            }

            /** @brief Returns whether the kernel has run the inner graph. */
            [[nodiscard]] bool is_settled() const noexcept
            {
                return state_.load(std::memory_order_acquire) != K_PENDING;
            }

            /** @brief Returns whether the inner run was refused with @p code. */
            [[nodiscard]] bool was_refused_with(ErrorCode code) const noexcept
            {
                return state_.load(std::memory_order_acquire) == static_cast<int>(code);
            }

            /** @brief Returns whether the inner run returned a report. */
            [[nodiscard]] bool has_completed() const noexcept
            {
                return state_.load(std::memory_order_acquire) == K_COMPLETED;
            }

        private:
            /// @brief K_PENDING, K_COMPLETED or the ErrorCode of the refusal.
            std::atomic<int> state_{K_PENDING};
    };

    /**
     * @brief Owns a graph of one task, out = 2 * in, built on a runtime the caller names.
     */
    class InnerStep final
    {
        public:
            /** @brief Builds and records the graph on @p runtime, which outlives this object. */
            explicit InnerStep(Runtime& runtime)
                : input_(runtime.buffer<float>(K_COUNT).value()),
                  output_(runtime.buffer<float>(K_COUNT).value()), graph_(runtime.graph())
            {
                float* source = input_.data();
                float* target = output_.data();
                for (std::size_t index = 0; index < K_COUNT; ++index)
                {
                    source[index] = static_cast<float>(index);
                    target[index] = K_UNWRITTEN;
                }

                NodeOptions options;
                options.name = "inner_double";
                graph_.add_task(
                    Reads(BufferRegion(source)), Writes(BufferRegion(target)),
                    [source, target](sycl::queue& queue,
                                     const std::vector<sycl::event>&) -> sycl::event
                    {
                        return queue.parallel_for(sycl::range<1>(K_COUNT), [=](sycl::id<1> index)
                        {
                            target[index[0]] = source[index[0]] * 2.0f;
                        });
                    }, options);
            }

            /** @brief Runs the graph once. */
            [[nodiscard]] Result<RunReport, Error> run()
            {
                return graph_.run();
            }

            /** @brief Returns whether every output element holds twice its input. */
            [[nodiscard]] bool is_doubled() const
            {
                for (std::size_t index = 0; index < K_COUNT; ++index)
                {
                    if (output_.data()[index] != 2.0f * input_.data()[index])
                        return false;
                }
                return true;
            }

            /** @brief Returns whether no element of the output was written. */
            [[nodiscard]] bool is_untouched() const
            {
                for (std::size_t index = 0; index < K_COUNT; ++index)
                {
                    if (output_.data()[index] != K_UNWRITTEN)
                        return false;
                }
                return true;
            }

        private:
            /// @brief The buffer the task reads.
            Buffer<float> input_;

            /// @brief The buffer the task writes.
            Buffer<float> output_;

            /// @brief The graph, recorded once.
            Graph graph_;
    };

    /**
     * @brief Returns a registry whose one kernel runs @p step from its own body.
     * @param step Outlives the registry.
     * @param outcome Receives how the run ended; outlives the registry.
     */
    sushi_ptr<KernelRegistry> nested_run_kernels(InnerStep& step, NestedOutcome& outcome)
    {
        sushi_ptr<KernelRegistry> kernels = make_sushi<KernelRegistry>();
        kernels->register_kernel(
            K_NESTED_RUN,
            [&step, &outcome](sycl::queue&, span<const BufferView>, span<const BufferView>,
                              const NodeOptions&) -> sycl::event
            {
                outcome.record(step.run());
                return sycl::event{};
            });
        return kernels;
    }

    /**
     * @brief Returns a registry whose one kernel does nothing; the master records its tasks
     *        against it while the worker holds the kernel that counts.
     */
    sushi_ptr<KernelRegistry> idle_kernels()
    {
        sushi_ptr<KernelRegistry> kernels = make_sushi<KernelRegistry>();
        kernels->register_kernel(
            K_NESTED_RUN,
            [](sycl::queue&, span<const BufferView>, span<const BufferView>,
               const NodeOptions&) -> sycl::event
            {
                return sycl::event{};
            });
        return kernels;
    }

    /**
     * @brief Runs a graph once on its own thread, so a run that never returns fails a test by
     *        timeout instead of hanging it.
     */
    class BoundedRun final
    {
        public:
            /** @brief Starts the run; @p graph outlives this object. */
            explicit BoundedRun(Graph& graph)
                : state_(std::make_shared<State>())
            {
                finished_ = state_->done.get_future();
                std::shared_ptr<State> state = state_;
                thread_ = std::thread([&graph, state]
                {
                    state->succeeded = graph.run().has_value();
                    state->done.set_value();
                });
            }

            /** @brief Joins a run that returned and detaches one that did not. */
            ~BoundedRun()
            {
                if (!thread_.joinable())
                    return;

                if (finished_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
                    thread_.join();
                else
                    thread_.detach();
            }

            BoundedRun(const BoundedRun&) = delete;
            BoundedRun& operator=(const BoundedRun&) = delete;
            BoundedRun(BoundedRun&&) = delete;
            BoundedRun& operator=(BoundedRun&&) = delete;

            /** @brief Returns whether the run returned within K_WAIT, and joins it if so. */
            [[nodiscard]] bool wait()
            {
                if (finished_.wait_for(K_WAIT) != std::future_status::ready)
                    return false;

                if (thread_.joinable())
                    thread_.join();
                return true;
            }

            /** @brief Returns whether the run that returned reported success. */
            [[nodiscard]] bool succeeded() const noexcept
            {
                return state_->succeeded;
            }

        private:
            /// @brief Holds what the run thread leaves for the test thread.
            struct State
            {
                std::promise<void> done; /**< Becomes ready when run() returned. */
                bool succeeded = false;  /**< Whether run() reported success; set before done. */
            };

            /// @brief Shared with the thread, which may outlive this object.
            std::shared_ptr<State> state_;

            /// @brief Becomes ready when run() returned.
            std::future<void> finished_;

            /// @brief The thread that calls run().
            std::thread thread_;
    };

    /**
     * @brief Records a task that runs the nested-run kernel once, over two buffers.
     * @param graph Receives the recorded task.
     */
    void record_nested_task(Graph& graph, const KernelRegistry& kernels, Buffer<float>& input,
                            Buffer<float>& output)
    {
        NodeOptions options;
        options.operation = K_NESTED_RUN;
        options.name = "nested_run";
        const BufferView inputs[1] = {BufferView{input.data(), K_COUNT * sizeof(float)}};
        const BufferView outputs[1] = {BufferView{output.data(), K_COUNT * sizeof(float)}};
        graph.extend([&](Recorder& recorder)
        {
            record_registered_task(recorder, kernels, options, span<const BufferView>(inputs, 1),
                                   span<const BufferView>(outputs, 1));
        });
    }

    /** @brief Returns the configuration of a second runtime that keeps one pool thread. */
    RuntimeConfig single_thread_config()
    {
        RuntimeConfig config;
        config.worker_count = 1;
        return config;
    }

    /** @brief Returns options whose policy sends a task of any size to a worker. */
    ClusterOptions eager_options()
    {
        ClusterOptions options;
        options.policy = std::make_shared<LeastLoadedOffloadPolicy>(0);
        return options;
    }

    /**
     * @brief Offloads one nested-run task to a worker whose kernel runs @p inner, and checks
     *        what the kernel saw.
     * @param worker_runtime The runtime the worker serves dispatches on.
     * @param outcome Receives how the kernel's run of @p inner ended.
     * @return Whether the master's run returned in time and reported success.
     */
    [[nodiscard]] bool offload_nested_run(Runtime& worker_runtime, InnerStep& inner,
                                          NestedOutcome& outcome)
    {
        InProcessFabric fabric;
        WorkerAgent agent(worker_runtime, InProcessTransport::make_worker(fabric),
                          nested_run_kernels(inner, outcome));

        Runtime master_runtime = Runtime::create().value();
        Buffer<float> input = master_runtime.buffer<float>(K_COUNT).value();
        Buffer<float> output = master_runtime.buffer<float>(K_COUNT).value();
        Cluster cluster = Cluster::attach(master_runtime, InProcessTransport::make_master(fabric),
                                          eager_options()).value();
        EXPECT_TRUE(agent.start().is_ok());
        EXPECT_TRUE(cluster.wait_for_workers(1, K_WAIT));
        cluster.share(input);
        cluster.share(output);

        sushi_ptr<KernelRegistry> master_kernels = idle_kernels();
        Graph graph = master_runtime.graph();
        record_nested_task(graph, *master_kernels, input, output);
        BoundedRun run(graph);
        const bool returned = run.wait();
        EXPECT_EQ(cluster.offloaded(), 1u) << "the task ran on the master, not on the worker";
        agent.stop();
        return returned && run.succeeded();
    }
} // namespace

/**
 * @brief Shows that a kernel running on a worker cannot run a graph of the worker's own runtime.
 *
 * The worker runs each dispatch as a task inside Graph::run(), so the engine's run marker is set
 * when the kernel starts, and the run entry refuses with InvalidState before it touches the pool.
 * It does not deadlock, and the worker's runtime is usable again once the dispatch ends.
 */
TEST(Medium_NestedRunProbe, WorkerKernelRunningAGraphOfItsOwnRuntimeIsRefused)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    Runtime worker_runtime = Runtime::create().value();
    InnerStep inner(worker_runtime);
    NestedOutcome outcome;

    ASSERT_TRUE(offload_nested_run(worker_runtime, inner, outcome))
        << "the dispatch never ended: the nested run deadlocked the worker";
    ASSERT_TRUE(outcome.is_settled());
    EXPECT_TRUE(outcome.was_refused_with(ErrorCode::InvalidState));
    EXPECT_TRUE(inner.is_untouched());

    // The refusal left the worker's runtime usable: the same graph runs once the dispatch is over.
    EXPECT_TRUE(inner.run().has_value());
    EXPECT_TRUE(inner.is_doubled());
}

/**
 * @brief Shows that a kernel running on a worker can run a graph of a second runtime.
 *
 * The second runtime has its own engine and so its own run marker, which no run holds. It costs a
 * second device context and its own pool threads; the test keeps the pool to one thread.
 */
TEST(Medium_NestedRunProbe, WorkerKernelRunsAGraphOfASecondRuntime)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    Runtime worker_runtime = Runtime::create().value();
    Runtime inner_runtime = Runtime::create(single_thread_config()).value();
    InnerStep inner(inner_runtime);
    NestedOutcome outcome;

    ASSERT_TRUE(offload_nested_run(worker_runtime, inner, outcome))
        << "the dispatch never ended: the second runtime's run hung";
    ASSERT_TRUE(outcome.is_settled());
    EXPECT_TRUE(outcome.has_completed());
    EXPECT_TRUE(inner.is_doubled());
}

/**
 * @brief Shows that a local fallback running on the master cannot run a graph of the master's
 *        own runtime.
 *
 * The fallback is the task's body inside the master's Graph::run(), the same body a task takes
 * back from a worker, so the engine's run marker is set and the run entry refuses with
 * InvalidState. It does not deadlock, and the runtime is usable again once the run ends.
 */
TEST(Medium_NestedRunProbe, MasterFallbackRunningAGraphOfItsOwnRuntimeIsRefused)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    Runtime master_runtime = Runtime::create().value();
    InnerStep inner(master_runtime);
    NestedOutcome outcome;
    sushi_ptr<KernelRegistry> kernels = nested_run_kernels(inner, outcome);
    Buffer<float> input = master_runtime.buffer<float>(K_COUNT).value();
    Buffer<float> output = master_runtime.buffer<float>(K_COUNT).value();

    Graph graph = master_runtime.graph();
    record_nested_task(graph, *kernels, input, output);
    BoundedRun run(graph);
    ASSERT_TRUE(run.wait()) << "the run never ended: the nested run deadlocked the master";

    EXPECT_TRUE(run.succeeded());
    ASSERT_TRUE(outcome.is_settled());
    EXPECT_TRUE(outcome.was_refused_with(ErrorCode::InvalidState));
    EXPECT_TRUE(inner.is_untouched());

    // The refusal left the runtime usable: the same graph runs once the outer run is over.
    EXPECT_TRUE(inner.run().has_value());
    EXPECT_TRUE(inner.is_doubled());
}

/**
 * @brief Shows that a local fallback running on the master can run a graph of a second runtime.
 *
 * The second runtime has its own engine and so its own run marker. It costs a second device
 * context and its own pool threads; the test keeps the pool to one thread.
 */
TEST(Medium_NestedRunProbe, MasterFallbackRunsAGraphOfASecondRuntime)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    Runtime master_runtime = Runtime::create().value();
    Runtime inner_runtime = Runtime::create(single_thread_config()).value();
    InnerStep inner(inner_runtime);
    NestedOutcome outcome;
    sushi_ptr<KernelRegistry> kernels = nested_run_kernels(inner, outcome);
    Buffer<float> input = master_runtime.buffer<float>(K_COUNT).value();
    Buffer<float> output = master_runtime.buffer<float>(K_COUNT).value();

    Graph graph = master_runtime.graph();
    record_nested_task(graph, *kernels, input, output);
    BoundedRun run(graph);
    ASSERT_TRUE(run.wait()) << "the run never ended: the second runtime's run hung";

    EXPECT_TRUE(run.succeeded());
    ASSERT_TRUE(outcome.is_settled());
    EXPECT_TRUE(outcome.has_completed());
    EXPECT_TRUE(inner.is_doubled());
}
