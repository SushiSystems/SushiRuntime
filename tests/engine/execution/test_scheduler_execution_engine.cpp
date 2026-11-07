/**************************************************************************/
/* test_scheduler_execution_engine.cpp                                    */
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
 * @file test_scheduler_execution_engine.cpp
 * @brief Tests graphs and tasks run through the scheduler the RuntimeContext assembles.
 * @see tests/engine/execution/README.md
 * @author Mustafa Garip
 */

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <numeric>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/core/clock.hpp>
#include <SushiRuntime/core/seeded_executor.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/graph/task_graph.hpp>
#include <SushiRuntime/task/task_types.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/memory/usm_request.hpp>
#include <SushiRuntime/region/buffer_region.hpp>
#include <SushiRuntime/graph/compiled_graph.hpp>
#include <SushiRuntime/execution/execution_engine.hpp>
#include <SushiRuntime/usm_guard/usm_shared.hpp>
#include <SushiRuntime/probe_tasks/atomic_counter_task.hpp>
#include <SushiRuntime/probe_tasks/order_recorder.hpp>
#include <SushiRuntime/probe_tasks/priority_tracking_task.hpp>
#include <SushiRuntime/scheduler/task_scheduler.hpp>
#include <SushiRuntime/runtime_assembly/context_factory.hpp>
#include <SushiRuntime/runtime_assembly/scheduler_factory.hpp>
#include <SushiRuntime/sycl/sycl_compat.hpp>

#include "support/host_context.hpp"
#include "support/host_runtime.hpp"
#include "support/tolerance.hpp"

using namespace SushiRuntime;

namespace
{
    /** @brief Returns the @p count floats at @p data as a vector. */
    std::vector<float> floats_of(const float* data, std::size_t count)
    {
        return std::vector<float>(data, data + count);
    }

    /** @brief Returns @p count floats whose element i is @p make(i). */
    std::vector<float> floats_from(std::size_t count, const std::function<float(std::size_t)>& make)
    {
        std::vector<float> values(count);
        for (std::size_t i = 0; i < count; ++i)
            values[i] = make(i);
        return values;
    }

    /**
     * @brief Tells whether every element of @p got is within the rounding bound of its element in @p want.
     *
     * @param operations The number of rounded operations on the longest path to one element.
     * @return A success, or a failure naming the first element out of bound.
     */
    ::testing::AssertionResult within_rounding(const std::vector<float>& got, const std::vector<float>& want,
                                               std::size_t operations)
    {
        if (got.size() != want.size())
            return ::testing::AssertionFailure() << "got " << got.size() << " elements, expected " << want.size();
        for (std::size_t i = 0; i < got.size(); ++i)
        {
            const TestSupport::ErrorRatio measured = TestSupport::measure_relative_error<float>(got[i], want[i], operations);
            if (!measured.is_within())
                return ::testing::AssertionFailure() << "element " << i << ": " << measured.describe();
        }
        return ::testing::AssertionSuccess();
    }
} // namespace

/** @brief Tests that CPU and GPU work on per-device queues runs concurrently and race-free. */
namespace AsyncExecutionTests
{
    using namespace SushiRuntime::Internal::Execution;
    using SushiRuntime::Test::USMShared;

    namespace
    {
        /**
         * @brief Finds the discovery index of the first CPU and first GPU.
         *
         * Iterates through all available SYCL devices to locate indices for CPU and GPU queues.
         * @param cpu_idx Reference to store the found CPU index.
         * @param gpu_idx Reference to store the found GPU index.
         * @return True if both a CPU and a GPU were found, false otherwise.
         */
        bool find_cpu_gpu_indices(size_t& cpu_idx, size_t& gpu_idx)
        {
            auto devs = sycl::device::get_devices(sycl::info::device_type::all);
            bool found_cpu = false;
            bool found_gpu = false;
            for (size_t i = 0; i < devs.size(); ++i)
            {
                if (!found_cpu && devs[i].is_cpu())
                {
                    cpu_idx = i;
                    found_cpu = true;
                }
                if (!found_gpu && devs[i].is_gpu())
                {
                    gpu_idx = i;
                    found_gpu = true;
                }
            }
            return found_cpu && found_gpu;
        }
    }

    /** @brief Fixture of the tests that need a CPU and a GPU queue of the context; they skip on a machine without both. */
    class Large_AsyncExecutionTest : public ::testing::Test
    {
        protected:
            /** @brief Builds the context and finds the two queues, or skips. */
            void SetUp() override
            {
                size_t cpu_idx = 0;
                size_t gpu_idx = 0;
                if (!find_cpu_gpu_indices(cpu_idx, gpu_idx))
                    GTEST_SKIP() << "Need both a CPU and a GPU SYCL device.";
                context_owner_ = SushiRuntime::Test::make_context();
                gpu_ = &context_owner_->devices().queue(gpu_idx);
                cpu_ = &context_owner_->devices().queue(cpu_idx);
            }

            /** @brief Returns the GPU queue. */
            sycl::queue& gpu_queue() { return *gpu_; }

            /** @brief Returns the CPU queue. */
            sycl::queue& cpu_queue() { return *cpu_; }

        private:
            std::shared_ptr<RuntimeContext> context_owner_; /**< The context of the test. */
            sycl::queue* gpu_ = nullptr;                    /**< The GPU queue of the context. */
            sycl::queue* cpu_ = nullptr;                    /**< The CPU queue of the context. */
    };

    /**
     * @brief Tests that vector-add on a CPU queue and on a GPU queue, submitted back-to-back, both produce their sums.
     *
     * Neither submit waits on the other, so the two devices run in parallel; see README.md.
     */
    TEST_F(Large_AsyncExecutionTest, CPUAndGPUVectorAddsBothProduceTheirSums)
    {
        constexpr size_t N = 1u << 16;
        sycl::queue& gpu_q = gpu_queue();
        sycl::queue& cpu_q = cpu_queue();
        USMShared<float> g_a(gpu_q, N), g_b(gpu_q, N), g_c(gpu_q, N);
        USMShared<float> c_a(cpu_q, N), c_b(cpu_q, N), c_c(cpu_q, N);
        ASSERT_TRUE(g_a);
        ASSERT_TRUE(g_b);
        ASSERT_TRUE(g_c);
        ASSERT_TRUE(c_a);
        ASSERT_TRUE(c_b);
        ASSERT_TRUE(c_c);
        float* ga = g_a.get();
        float* gb = g_b.get();
        float* gc = g_c.get();
        float* ca = c_a.get();
        float* cb = c_b.get();
        float* cc = c_c.get();
        std::iota(ga, ga + N, 0.f);
        std::transform(ga, ga + N, gb, [](float value) { return value * 2.f; });
        std::fill(gc, gc + N, 0.f);
        std::iota(ca, ca + N, 1.f);
        std::transform(ga, ga + N, cb, [](float value) { return value * 3.f; });
        std::fill(cc, cc + N, 0.f);

        sycl::event gpu_ev = gpu_q.submit([&](sycl::handler& h)
        {
            h.parallel_for(sycl::range<1>(N), [=](sycl::id<1> i) { gc[i] = ga[i] + gb[i]; });
        });
        sycl::event cpu_ev = cpu_q.submit([&](sycl::handler& h)
        {
            h.parallel_for(sycl::range<1>(N), [=](sycl::id<1> i) { cc[i] = ca[i] + cb[i]; });
        });
        cpu_ev.wait_and_throw();
        gpu_ev.wait_and_throw();

        EXPECT_EQ(floats_of(gc, N), floats_from(N, [](size_t i) { return float(i) + float(i) * 2.f; }))
            << "GPU half of the concurrent execution produced wrong results.";
        EXPECT_EQ(floats_of(cc, N), floats_from(N, [](size_t i) { return (float(i) + 1.f) + float(i) * 3.f; }))
            << "CPU half of the concurrent execution produced wrong results.";
    }

    /**
     * @brief Verifies that asynchronous GPU dispatch does not block the host.
     *
     * Submits a host task that waits on a gate to the GPU queue. The task cannot finish before the gate opens,
     * so an event that is not complete when submit returns shows that submit did not wait for it.
     */
    TEST_F(Large_AsyncExecutionTest, AsyncDispatchDoesNotBlock)
    {
        std::promise<void> gate;
        const std::shared_future<void> opened = gate.get_future().share();

        sycl::event ev = gpu_queue().submit([opened](sycl::handler& h)
        {
            SushiRuntime::Detail::enqueue_host_task(h, [opened] { opened.wait(); });
        });
        const bool in_flight = ev.get_info<sycl::info::event::command_execution_status>()
                               != sycl::info::event_command_status::complete;
        gate.set_value();
        ev.wait_and_throw();

        EXPECT_TRUE(in_flight) << "GPU submit returned only after the task completed.";
    }
} // namespace AsyncExecutionTests

/** @brief Tests a diamond of four device-kernel nodes added with add_node(TaskWork, reads, writes). */
namespace DevicePipelineTests
{
    using namespace SushiRuntime::Internal::Execution;
    using namespace SushiRuntime::Internal::Graph;
    using SushiRuntime::Test::USMShared;

    namespace
    {
        /**
         * @brief Checks if NUMA node 0 maps to a single device queue.
         *
         * The engine round-robins KERNEL nodes across a node's queues, so with more than one queue the nodes scatter across contexts and shared USM is no longer universally valid.
         * @param context The runtime context to query for NUMA queues.
         * @return True if NUMA node 0 has exactly one device queue, false otherwise.
         */
        bool numa0_has_single_queue(RuntimeContext& context)
        {
            sycl::queue& first = context.devices().queue_or_numa_local(std::nullopt, 0);
            for (int i = 0; i < 6; ++i)
                if (&context.devices().queue_or_numa_local(std::nullopt, 0) != &first)
                    return false;
            return true;
        }
    }

    /**
     * @brief Tests an engine-driven device kernel diamond pipeline.
     *
     * Verifies the engine resolves the RAW fan-out, keeps the readers parallel, joins the fan-in, and produces correct results across every buffer.
     */
    TEST(Medium_DevicePipelineTest, DiamondAllKernelNodes)
    {
        const auto context_owner = SushiRuntime::Test::make_context();
        RuntimeContext& context = *context_owner;

        if (!numa0_has_single_queue(context))
            GTEST_SKIP() << "NUMA 0 exposes multiple device queues; the engine would scatter "
                            "KERNEL nodes across USM contexts. Cross-device paths are covered "
                            "by HeterogeneousPipelineTest / AsyncOverlapBench.";

        sycl::queue& queue = context.devices().queue_or_numa_local(std::nullopt, 0);

        constexpr size_t N = 1024;
        USMShared<float> A(queue, N), B(queue, N), C(queue, N), D(queue, N);
        ASSERT_TRUE(A);
        ASSERT_TRUE(B);
        ASSERT_TRUE(C);
        ASSERT_TRUE(D);

        float* a = A.get();
        float* b = B.get();
        float* c = C.get();
        float* d = D.get();

        TaskGraph graph;

        // init: A[i] = i
        graph.add_node(
            [a](sycl::handler& h)
            {
                h.parallel_for(sycl::range<1>(N), [=](sycl::id<1> i) { a[i] = float(i); });
            },
            {}, {SushiRuntime::BufferRegion(a)});

        // doubleA: B[i] = A[i] * 2
        graph.add_node(
            [a, b](sycl::handler& h)
            {
                h.parallel_for(sycl::range<1>(N), [=](sycl::id<1> i) { b[i] = a[i] * 2.0f; });
            },
            {SushiRuntime::BufferRegion(a)}, {SushiRuntime::BufferRegion(b)});

        // incA: C[i] = A[i] + 1  (parallel sibling of doubleA)
        graph.add_node(
            [a, c](sycl::handler& h)
            {
                h.parallel_for(sycl::range<1>(N), [=](sycl::id<1> i) { c[i] = a[i] + 1.0f; });
            },
            {SushiRuntime::BufferRegion(a)}, {SushiRuntime::BufferRegion(c)});

        // combine: D[i] = B[i] + C[i]  (fan-in)
        graph.add_node(
            [b, c, d](sycl::handler& h)
            {
                h.parallel_for(sycl::range<1>(N), [=](sycl::id<1> i) { d[i] = b[i] + c[i]; });
            },
            {SushiRuntime::BufferRegion(b), SushiRuntime::BufferRegion(c)}, {SushiRuntime::BufferRegion(d)});

        auto compiled = graph.compile().value();
        EXPECT_EQ(compiled.roots.size(), 1u) << "Only the init node should be a root of the diamond.";

        context.execution_engine().execute(std::move(compiled)).value();
        context.devices().wait_all().value();

        EXPECT_TRUE(within_rounding(floats_of(b, N), floats_from(N, [](size_t i) { return float(i) * 2.0f; }), 2))
            << "Buffer B of the device-kernel diamond is wrong.";
        EXPECT_TRUE(within_rounding(floats_of(c, N), floats_from(N, [](size_t i) { return float(i) + 1.0f; }), 2))
            << "Buffer C of the device-kernel diamond is wrong.";
        EXPECT_TRUE(within_rounding(floats_of(d, N), floats_from(N, [](size_t i) { return 3.0f * float(i) + 1.0f; }), 4))
            << "Buffer D of the device-kernel diamond is wrong.";
    }
} // namespace DevicePipelineTests

/** @brief Tests many independent host, device, host chains running at once through the scheduler. */
namespace HeterogeneousPipelineTests
{
    using namespace SushiRuntime::Internal::Execution;
    using namespace SushiRuntime::Internal::Graph;
    using SushiRuntime::Test::USMShared;

    namespace
    {
        constexpr int    K_CHAINS = 16;
        constexpr size_t N      = 2048;
        constexpr float  K_SCALE  = 3.0f;
        constexpr float  K_BIAS   = 5.0f;

        /** @brief Adds an init, scale and bias chain of device kernels on @p dq over @p data. */
        void add_device_chain(TaskGraph& graph, sycl::queue* dq, float* data, int c)
        {
            TaskMetadata meta_init{"chain_init_dev"};
            graph.add_task(meta_init, {}, {SushiRuntime::BufferRegion(data)},
                [data, c, dq](sycl::queue&, const std::vector<sycl::event>&) -> sycl::event
                {
                    return dq->submit([&](sycl::handler& h)
                    {
                        h.parallel_for(sycl::range<1>(N), [=](sycl::id<1> i)
                        {
                            data[i] = float(i) + float(c);
                        });
                    });
                });

            TaskMetadata meta_scale{"chain_scale_dev"};
            graph.add_task(meta_scale, {SushiRuntime::BufferRegion(data)}, {SushiRuntime::BufferRegion(data)},
                [data, dq](sycl::queue&, const std::vector<sycl::event>& dependencies) -> sycl::event
                {
                    return dq->submit([&](sycl::handler& h)
                    {
                        h.depends_on(dependencies);
                        h.parallel_for(sycl::range<1>(N), [=](sycl::id<1> i)
                        {
                            data[i] = data[i] * K_SCALE;
                        });
                    });
                });

            TaskMetadata meta_bias{"chain_bias_dev"};
            graph.add_task(meta_bias, {SushiRuntime::BufferRegion(data)}, {SushiRuntime::BufferRegion(data)},
                [data, dq](sycl::queue&, const std::vector<sycl::event>& dependencies) -> sycl::event
                {
                    return dq->submit([&](sycl::handler& h)
                    {
                        h.depends_on(dependencies);
                        h.parallel_for(sycl::range<1>(N), [=](sycl::id<1> i)
                        {
                            data[i] = data[i] + K_BIAS;
                        });
                    });
                });
        }

        /** @brief Adds an init, scale and bias chain of host tasks over @p data. */
        void add_host_chain(TaskGraph& graph, float* data, int c)
        {
            TaskMetadata meta_init{"chain_init_host"};
            graph.add_task(meta_init, {}, {SushiRuntime::BufferRegion(data)},
                [data, c](sycl::queue&, const std::vector<sycl::event>&) -> sycl::event
                {
                    std::iota(data, data + N, float(c));
                    return {};
                });

            TaskMetadata meta_scale{"chain_scale_host"};
            graph.add_task(meta_scale, {SushiRuntime::BufferRegion(data)}, {SushiRuntime::BufferRegion(data)},
                [data](sycl::queue&, const std::vector<sycl::event>&) -> sycl::event
                {
                    std::transform(data, data + N, data, [](float value) { return value * K_SCALE; });
                    return {};
                });

            TaskMetadata meta_bias{"chain_bias_host"};
            graph.add_task(meta_bias, {SushiRuntime::BufferRegion(data)}, {SushiRuntime::BufferRegion(data)},
                [data](sycl::queue&, const std::vector<sycl::event>&) -> sycl::event
                {
                    std::transform(data, data + N, data, [](float value) { return value + K_BIAS; });
                    return {};
                });
        }

        /** @brief Returns the queues of the context that sit on a CPU or a GPU, from its deduplicated set. */
        std::vector<sycl::queue*> cpu_and_gpu_queues(RuntimeContext& context)
        {
            std::vector<sycl::queue*> queues;
            for (size_t i = 0; i < context.devices().count(); ++i)
            {
                sycl::queue& queue = context.devices().queue(i);
                if (queue.get_device().is_gpu() || queue.get_device().is_cpu())
                    queues.push_back(&queue);
            }
            return queues;
        }

        /** @brief Returns the values of every chain, one after the other: chain c holds (i + c) * K_SCALE + K_BIAS. */
        std::vector<float> expected_chains()
        {
            return floats_from(K_CHAINS * N, [](size_t k)
            {
                return (float(k % N) + float(k / N)) * K_SCALE + K_BIAS;
            });
        }
    } // namespace

    /**
     * @brief Tests concurrent execution of multiple heterogeneous pipelines.
     *
     * Independent init -> scale -> bias chains run concurrently, split across GPU as kernels and CPU as host work.
     */
    TEST(Medium_HeterogeneousPipelineTest, ManyChainsHostDeviceHost)
    {
        const auto context_owner = SushiRuntime::Test::make_context();
        RuntimeContext& context = *context_owner;
        TaskGraph graph;
        const std::vector<sycl::queue*> device_queues = cpu_and_gpu_queues(context);
        ASSERT_FALSE(device_queues.empty()) << "No CPU/GPU device queues available.";

        // Pin each chain to one device queue so its buffer and kernels share a context.
        std::vector<USMShared<float>> buffers;
        buffers.reserve(K_CHAINS);
        for (int c = 0; c < K_CHAINS; ++c)
        {
            sycl::queue& dq = *device_queues[static_cast<size_t>(c) % device_queues.size()];
            buffers.emplace_back(dq, N);
            ASSERT_TRUE(buffers.back()) << "USM allocation failed for chain " << c;
            sycl::queue* const chain_queue = &dq;
            if (dq.get_device().is_gpu())
                add_device_chain(graph, chain_queue, buffers.back().get(), c);
            else
                add_host_chain(graph, buffers.back().get(), c);
        }

        auto compiled = graph.compile().value();
        context.execution_engine().execute(std::move(compiled)).value();
        context.devices().wait_all().value();

        std::vector<float> got;
        got.reserve(K_CHAINS * N);
        for (const USMShared<float>& buffer : buffers)
            got.insert(got.end(), buffer.get(), buffer.get() + N);
        EXPECT_TRUE(within_rounding(got, expected_chains(), 4));
    }
} // namespace HeterogeneousPipelineTests

/** @brief Tests an end-to-end vector addition and a multi-stage chain through the TaskGraph. */
namespace SYCLExecutionTests
{
    using namespace SushiRuntime::Internal::Execution;
    using namespace SushiRuntime::Internal::Graph;
    using SushiRuntime::Test::USMShared;

    /**
     * @brief Tests an end-to-end vector addition.
     *
     * A host task initializes the inputs, a device kernel adds them, and the host verifies the result.
     */
    TEST(Medium_SyclExecutionTest, EndToEndVectorAddition)
    {
        const auto context_owner = SushiRuntime::Test::make_context();
        RuntimeContext& context = *context_owner;
        TaskGraph graph;

        sycl::queue& device_queue = context.devices().queue_or_numa_local(std::nullopt, 0);

        constexpr size_t VECTOR_SIZE = 1024;

        USMShared<float> usm_A(device_queue, VECTOR_SIZE);
        USMShared<float> usm_B(device_queue, VECTOR_SIZE);
        USMShared<float> usm_C(device_queue, VECTOR_SIZE);

        ASSERT_TRUE(usm_A);
        ASSERT_TRUE(usm_B);
        ASSERT_TRUE(usm_C);

        float* A = usm_A.get();
        float* B = usm_B.get();
        float* C = usm_C.get();

        TaskMetadata meta_init{"Init_Vectors"};
        auto init_work = [A, B, C](sycl::queue&, const std::vector<sycl::event>&) -> sycl::event
        {
            std::iota(A, A + VECTOR_SIZE, 0.0f);
            std::transform(A, A + VECTOR_SIZE, B, [](float value) { return value * 2.0f; });
            std::fill(C, C + VECTOR_SIZE, 0.0f);
            return {};
        };
        graph.add_task(meta_init, {}, {SushiRuntime::BufferRegion(A), SushiRuntime::BufferRegion(B), SushiRuntime::BufferRegion(C)}, init_work);

        TaskMetadata meta_compute{"Vector_Add_Kernel"};
        auto compute_work = [A, B, C](sycl::queue& queue,
                                      const std::vector<sycl::event>& dependencies) -> sycl::event
        {
            return queue.submit([&](sycl::handler& h)
            {
                h.depends_on(dependencies);
                h.parallel_for(sycl::range<1>(VECTOR_SIZE), [=](sycl::id<1> idx)
                {
                    C[idx] = A[idx] + B[idx];
                });
            });
        };
        graph.add_task(meta_compute, {SushiRuntime::BufferRegion(A), SushiRuntime::BufferRegion(B)}, {SushiRuntime::BufferRegion(C)}, compute_work);

        auto compiled = graph.compile().value();
        context.execution_engine().execute(std::move(compiled)).value();

        EXPECT_EQ(floats_of(C, VECTOR_SIZE),
                  floats_from(VECTOR_SIZE, [](size_t i) { return static_cast<float>(i) + static_cast<float>(i * 2); }))
            << "Vector addition kernel produced incorrect results!";
    }

    /**
     * @brief Tests a multi-stage execution chain sharing USM.
     *
     * Three dependent stages: init (host) -> scale (device kernel) -> reduce (host sum). The tracker must serialize them and the final sum must match the expected value.
     */
    TEST(Medium_SyclExecutionTest, MultiStageChain)
    {
        const auto context_owner = SushiRuntime::Test::make_context();
        RuntimeContext& context = *context_owner;
        TaskGraph graph;

        constexpr size_t N     = 2048;
        constexpr float  SCALE = 3.0f;

        // Both buffers are pinned to device 0, as a task's buffers live on one device; see README.md.
        float* data = static_cast<float*>(
            context.memory().allocate({Internal::Memory::USMKind::Shared, N * sizeof(float), 0, 0}));
        double* sum = static_cast<double*>(
            context.memory().allocate({Internal::Memory::USMKind::Shared, sizeof(double), 0, 0}));
        ASSERT_NE(data, nullptr);
        ASSERT_NE(sum, nullptr);

        TaskMetadata meta_init{"Init"};
        graph.add_task(meta_init, {}, {SushiRuntime::BufferRegion(data)}, [data](sycl::queue&, const std::vector<sycl::event>&) -> sycl::event
        {
            for (size_t i = 0; i < N; ++i)
                data[i] = static_cast<float>(i);
            return {};
        });

        TaskMetadata meta_scale{"Scale"};
        graph.add_task(meta_scale, {SushiRuntime::BufferRegion(data)}, {SushiRuntime::BufferRegion(data)},
                       [data](sycl::queue& queue, const std::vector<sycl::event>& dependencies)
                           -> sycl::event
        {
            return queue.submit([&](sycl::handler& h)
            {
                h.depends_on(dependencies);
                h.parallel_for(sycl::range<1>(N), [=](sycl::id<1> idx)
                {
                    data[idx] = data[idx] * SCALE;
                });
            });
        });

        TaskMetadata meta_reduce{"Reduce"};
        graph.add_task(meta_reduce, {SushiRuntime::BufferRegion(data)}, {SushiRuntime::BufferRegion(sum)}, [data, sum](sycl::queue&, const std::vector<sycl::event>&) -> sycl::event
        {
            double acc = 0.0;
            for (size_t i = 0; i < N; ++i)
                acc += data[i];
            *sum = acc;
            return {};
        });

        auto compiled = graph.compile().value();
        context.execution_engine().execute(std::move(compiled)).value();

        // SCALE * sum_{i=0}^{N-1} i = SCALE * (N-1)*N/2.
        double expected = static_cast<double>(SCALE) *
                          (static_cast<double>(N - 1) * static_cast<double>(N) / 2.0);

        EXPECT_TRUE(TestSupport::is_within(TestSupport::measure_sum_error<double>(*sum, expected, expected, N)))
            << "Multi-stage pipeline produced an incorrect reduction.";

        context.memory().release(data);
        context.memory().release(sum);
    }
} // namespace SYCLExecutionTests

/** @brief Tests that a replayed plan reads what the host wrote between two executions. */
namespace ReplayHostWriteTests
{
    namespace
    {
        using SushiRuntime::Internal::Execution::ExecutionEngine;
        using SushiRuntime::Internal::Execution::RuntimeContext;
        using SushiRuntime::Internal::Graph::CompiledGraph;
        using SushiRuntime::Internal::Graph::TaskGraph;
        using SushiRuntime::BufferRegion;
        using SushiRuntime::Internal::Memory::USMKind;

        /** @brief Holds how many times each plan is replayed. */
        constexpr std::uint32_t K_REPLAYS = 8;

        /** @brief Owns one word of USM on one device of a context. */
        class Word final
        {
            public:
                /** @brief Allocates the word through @p context, which outlives this object. */
                Word(RuntimeContext& context, USMKind kind, std::size_t device)
                    : context_(context),
                      data_(static_cast<std::uint32_t*>(context.memory().allocate(
                          {kind, sizeof(std::uint32_t), 0, device})))
                {

                }

                /** @brief Releases the word. */
                ~Word()
                {
                    if (data_ != nullptr)
                        context_.memory().release(data_);
                }

                Word(const Word&) = delete;
                Word& operator=(const Word&) = delete;
                Word(Word&&) = delete;
                Word& operator=(Word&&) = delete;

                /** @brief Returns the word, or nullptr when the allocation failed. */
                [[nodiscard]] std::uint32_t* data() const noexcept
                {
                    return data_;
                }

            private:
                RuntimeContext& context_; /**< The context that allocated the word. */
                std::uint32_t* data_;     /**< The word. */
        };

        /** @brief Compiles a plan of one kernel that writes @p output as @p input plus one. */
        CompiledGraph compile_increment(std::uint32_t* input, std::uint32_t* output)
        {
            const std::vector<BufferRegion> reads{BufferRegion(input)};
            const std::vector<BufferRegion> writes{BufferRegion(output)};

            TaskGraph graph;
            graph.add_node([input, output](sycl::handler& handler)
            {
                handler.single_task([=]
                {
                    *output = *input + 1u;
                });
            }, reads, writes);

            CompiledGraph plan = graph.compile().value();
            plan.capture_base_state();
            return plan;
        }

        /** @brief Replays one increment plan on @p device around a host write of its input. */
        void replay_around_host_writes(RuntimeContext& context, USMKind input_kind, std::size_t device)
        {
            const Word input(context, input_kind, device);
            const Word output(context, USMKind::Shared, device);
            ASSERT_NE(input.data(), nullptr);
            ASSERT_NE(output.data(), nullptr);

            ExecutionEngine& engine = context.execution_engine();
            CompiledGraph plan = compile_increment(input.data(), output.data());

            for (std::uint32_t value = 0; value < K_REPLAYS; ++value)
            {
                *input.data() = value;
                if (value != 0)
                    plan.rearm();
                engine.execute(plan).value();
                EXPECT_EQ(*output.data(), value + 1u)
                    << "device " << device << ", replay " << value;
            }
        }

        /** @brief Replays the increment plan on every device of @p context with an input of @p input_kind. */
        void replay_on_every_device(RuntimeContext& context, USMKind input_kind)
        {
            for (std::size_t device = 0; device < context.devices().count(); ++device)
                replay_around_host_writes(context, input_kind, device);
        }
    } // namespace

    /** @brief Fixture of the rows that replay a plan over an input word of one USM kind. */
    class Medium_ReplayHostWrite : public ::testing::TestWithParam<USMKind>
    {
    };

    /** @brief Tests that a replay reads the word of the row's kind the host wrote since the last execution. */
    TEST_P(Medium_ReplayHostWrite, AReplayReadsTheWordTheHostWrote)
    {
        const auto context_owner = SushiRuntime::Test::make_context();

        replay_on_every_device(*context_owner, GetParam());
    }

    INSTANTIATE_TEST_SUITE_P(Rows, Medium_ReplayHostWrite, ::testing::Values(USMKind::Shared, USMKind::Host));
} // namespace ReplayHostWriteTests

/** @brief Tests TaskGraph ordering for a sequential dependency, independent roots and a diamond. */
namespace GraphTests
{
    using namespace SushiRuntime::Internal::Execution;
    using namespace SushiRuntime::Internal::Graph;

    /**
     * @brief Tests sequential execution due to memory dependencies.
     *
     * Three nodes sharing one cell must run strictly in order (RAW, then WAR/WAW).
     */
    TEST(Small_TaskGraphTest, SequentialUSMDependency)
    {
        const auto ctx_owner = TestSupport::make_host_context();
        RuntimeContext& ctx = *ctx_owner;
        TaskGraph graph;

        int shared_cell = 0;
        const BufferRegion cell(&shared_cell);
        std::vector<int> execution_order;

        graph.add_native({}, {cell}, [&execution_order] { execution_order.push_back(1); });
        graph.add_native({cell}, {}, [&execution_order] { execution_order.push_back(2); });
        graph.add_native({}, {cell}, [&execution_order] { execution_order.push_back(3); });

        auto compiled_graph = graph.compile().value();
        EXPECT_EQ(compiled_graph.roots.size(), 1u) << "Graph should only have 1 root due to linear dependency.";

        ctx.execution_engine().execute(compiled_graph).value();

        EXPECT_EQ(execution_order, (std::vector<int>{1, 2, 3}));
    }

    /**
     * @brief Tests that independent nodes are all roots and all run.
     *
     * Nodes with no shared memory are independent roots.
     */
    TEST(Small_TaskGraphTest, IndependentRootsExecution)
    {
        const auto ctx_owner = TestSupport::make_host_context();
        RuntimeContext& ctx = *ctx_owner;
        TaskGraph graph;

        constexpr std::size_t NUM_TASKS = 10;
        std::size_t completed_tasks = 0;

        for (std::size_t i = 0; i < NUM_TASKS; ++i)
            graph.add_native({}, {}, [&completed_tasks] { ++completed_tasks; });

        auto compiled_graph = graph.compile().value();

        EXPECT_EQ(compiled_graph.roots.size(), NUM_TASKS);

        ctx.execution_engine().execute(compiled_graph).value();

        EXPECT_EQ(completed_tasks, NUM_TASKS);
    }

    /**
     * @brief Tests execution of a diamond DAG.
     *
     * A(write X) -> B,C(read X) -> D(write X). B and C run after A; D waits for both resolving the WAR hazard.
     */
    TEST(Small_TaskGraphTest, FanOutFanInDiamond)
    {
        const auto ctx_owner = TestSupport::make_host_context();
        RuntimeContext& ctx = *ctx_owner;
        TaskGraph graph;

        int shared_resource = 0;
        const BufferRegion cell(&shared_resource);
        int current_phase = 0;
        int readers_after_writer = 0;

        graph.add_native({}, {cell}, [&current_phase] { current_phase = 1; });
        auto reader = [&current_phase, &readers_after_writer]
        {
            if (current_phase >= 1)
                ++readers_after_writer;
        };
        graph.add_native({cell}, {}, reader);
        graph.add_native({cell}, {}, reader);
        graph.add_native({}, {cell}, [&current_phase, &readers_after_writer]
        {
            current_phase = readers_after_writer == 2 ? 3 : -1;
        });

        auto compiled = graph.compile().value();
        ctx.execution_engine().execute(compiled).value();

        EXPECT_EQ(readers_after_writer, 2) << "Readers executed before the initial writer completed!";
        EXPECT_EQ(current_phase, 3) << "Final writer ran before both readers finished.";
    }
} // namespace GraphTests

/** @brief Tests that one armed scheduler runs every task of each step it is handed in turn. */
namespace SchedulerStepTests
{
    using namespace SushiRuntime::Internal::Execution;
    using namespace SushiRuntime::Internal::Scheduler;

    namespace
    {
        /** @brief Holds the number of independent tasks each step submits. */
        constexpr std::size_t K_TASKS_PER_STEP = 32;

        /**
         * @brief Submits one step of counting host nodes, runs its rounds and waits until every one has retired.
         * @param scheduler Armed scheduler to submit to; its rounds run on @p executor.
         * @param executor The executor the scheduler posts its rounds to.
         * @param nodes Receives the step's nodes, which must outlive the step.
         * @param executed Counter the nodes increment.
         * @return The counter value once the step has drained.
         */
        std::size_t drain_step(TaskScheduler& scheduler, SushiRuntime::SeededExecutor& executor,
                               std::vector<SushiRuntime::sushi_ptr<SushiRuntime::Internal::Graph::Node>>& nodes,
                               std::atomic<std::size_t>& executed)
        {
            using SushiRuntime::Internal::Graph::Node;
            for (std::size_t k = 0; k < K_TASKS_PER_STEP; ++k)
            {
                SushiRuntime::sushi_ptr<Node> node = SushiRuntime::make_sushi<Node>();
                node->mode = Node::ExecutionMode::Host;
                node->submits_device_work_ = false;
                node->native_work = [&executed] { executed.fetch_add(1, std::memory_order_relaxed); };
                scheduler.submit(node.get());
                nodes.push_back(std::move(node));
            }
            while (executor.run_one())
            {
            }
            scheduler.wait_quiescent();
            return executed.load(std::memory_order_acquire);
        }
    } // namespace

    /** @brief Tests that each of three consecutive steps runs all of its tasks and no more. */
    TEST(Small_SchedulerSteps, EachStepRunsAllOfItsTasksOnTheArmedScheduler)
    {
        SushiRuntime::SeededExecutor executor(TestSupport::K_HOST_RUNTIME_SEED);
        const auto context = TestSupport::make_host_context();
        TaskScheduler scheduler(WorkerPoolSettings{2, 0, SushiRuntime::PinPolicy::Float}, context->topology(),
                                context->devices(), context->memory());
        scheduler.set_executor(&executor);
        std::vector<SushiRuntime::sushi_ptr<SushiRuntime::Internal::Graph::Node>> nodes;
        std::atomic<std::size_t> executed{0};

        scheduler.start();
        scheduler.arm_for_execution();

        EXPECT_EQ(drain_step(scheduler, executor, nodes, executed), K_TASKS_PER_STEP);
        EXPECT_EQ(drain_step(scheduler, executor, nodes, executed), 2 * K_TASKS_PER_STEP);
        EXPECT_EQ(drain_step(scheduler, executor, nodes, executed), 3 * K_TASKS_PER_STEP);

        scheduler.stop();
    }
} // namespace SchedulerStepTests

/** @brief Tests the task scheduler at scale: a task flood, producer contention and rapid restarts. */
namespace SchedulerStressTests
{
    using namespace SushiRuntime::Internal::Execution;
    using namespace SushiRuntime::Internal::Scheduler;
    using SushiRuntime::Test::make_scheduler;
    using SushiRuntime::Test::AtomicCounterTask;
    using SushiRuntime::Test::PriorityTrackingTask;

    namespace
    {
        /** @brief Holds how many times the arm and stop cycle runs. */
        constexpr int K_CYCLES = 500;

        /** @brief Holds how many tasks each cycle submits before it stops abruptly. */
        constexpr std::size_t K_TASKS_PER_CYCLE = 1000;

        /** @brief Arms a fresh scheduler, submits the cycle's tasks, stops it at once and returns how many ran. */
        std::size_t executed_in_one_cycle(RuntimeContext& context)
        {
            TaskScheduler scheduler = make_scheduler(context);
            std::atomic<size_t> executed_count{0};

            scheduler.start();
            scheduler.arm_for_execution();
            for (std::size_t j = 0; j < K_TASKS_PER_CYCLE; ++j)
                scheduler.submit(new AtomicCounterTask(executed_count));
            scheduler.stop();

            return executed_count.load();
        }
    } // namespace

    /**
     * @brief Tests scheduler stability and deque resizing under heavy load.
     *
     * Submits one million tasks at once.
     */
    TEST(Large_TaskSchedulerStress, OneMillionTaskBomb)
    {
        const auto ctx_owner = SushiRuntime::Test::make_context();
        RuntimeContext& ctx = *ctx_owner;
        TaskScheduler scheduler = make_scheduler(ctx);

        std::atomic<size_t> executed_count{0};
        constexpr size_t TOTAL_TASKS = 1'000'000;

        scheduler.start();
        scheduler.arm_for_execution();
        for (size_t i = 0; i < TOTAL_TASKS; ++i)
            scheduler.submit(new AtomicCounterTask(executed_count));

        scheduler.wait_quiescent();
        scheduler.stop();
        EXPECT_EQ(executed_count.load(), TOTAL_TASKS);
    }

    /**
     * @brief Tests submission under contention.
     *
     * Uses 8 producer threads submitting at once.
     */
    TEST(Large_TaskSchedulerStress, MultiProducerContention)
    {
        const auto ctx_owner = SushiRuntime::Test::make_context();
        RuntimeContext& ctx = *ctx_owner;
        TaskScheduler scheduler = make_scheduler(ctx);

        std::atomic<size_t> executed_count{0};
        constexpr int    NUM_PRODUCERS      = 8;
        constexpr size_t TASKS_PER_PRODUCER = 50'000;

        scheduler.start();
        scheduler.arm_for_execution();

        std::vector<std::thread> producers;
        for (int p = 0; p < NUM_PRODUCERS; ++p)
        {
            producers.emplace_back([&scheduler, &executed_count]()
            {
                for (size_t i = 0; i < TASKS_PER_PRODUCER; ++i)
                    scheduler.submit(new AtomicCounterTask(executed_count));
            });
        }
        for (auto& t : producers)
            t.join();

        scheduler.wait_quiescent();
        scheduler.stop();
        EXPECT_EQ(executed_count.load(), NUM_PRODUCERS * TASKS_PER_PRODUCER);
    }

    /**
     * @brief Tests repeatedly arming and stopping the scheduler.
     *
     * Helps to shake out races, deadlocks, and leaks. Each cycle stops right after its submits, and
     * stop() drains, so every cycle runs all of its tasks.
     */
    TEST(Large_TaskSchedulerStress, RapidArmAndStopThrashing)
    {
        const auto ctx_owner = SushiRuntime::Test::make_context();
        RuntimeContext& ctx = *ctx_owner;
        std::vector<std::size_t> executed(K_CYCLES);

        std::generate(executed.begin(), executed.end(), [&ctx] { return executed_in_one_cycle(ctx); });

        EXPECT_EQ(executed, std::vector<std::size_t>(K_CYCLES, K_TASKS_PER_CYCLE));
    }

    /**
     * @brief Tests high-priority task bypassing.
     *
     * A high-priority task must bypass the queue even behind many low-priority ones.
     */
    TEST(Large_TaskSchedulerStress, PriorityBypassUnderLoad)
    {
        const auto ctx_owner = SushiRuntime::Test::make_context();
        RuntimeContext& ctx = *ctx_owner;
        TaskScheduler scheduler = make_scheduler(ctx);

        std::vector<int> execution_order;
        std::mutex order_mutex;

        scheduler.start();
        // Do NOT arm yet, so tasks stay in the injector.
        for (int i = 0; i < 50; ++i)
            scheduler.submit(new PriorityTrackingTask(0, execution_order, order_mutex));
        scheduler.submit(new PriorityTrackingTask(100, execution_order, order_mutex));
        scheduler.arm_for_execution();

        scheduler.wait_quiescent();
        scheduler.stop();

        ASSERT_EQ(execution_order.size(), 51u);
        const std::size_t window = std::min<std::size_t>(scheduler.worker_count() * 2, execution_order.size());
        const auto first_batch_end = execution_order.begin() + static_cast<std::ptrdiff_t>(window);
        EXPECT_NE(std::find(execution_order.begin(), first_batch_end, 100), first_batch_end)
            << "High priority task was not executed in the first batch!";
    }
} // namespace SchedulerStressTests
