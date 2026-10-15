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
 * @file test_scheduler_execution_engine.cpp
 * @brief Tests graphs and tasks run through the scheduler the RuntimeContext assembles.
 * @see tests/engine/execution/README.md
 * @author Mustafa Garip
 */

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/graph/task_graph.hpp>
#include <SushiRuntime/task/task_types.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/memory/usm_request.hpp>
#include <SushiRuntime/region/buffer_region.hpp>
#include <SushiRuntime/graph/compiled_graph.hpp>
#include <SushiRuntime/execution/execution_engine.hpp>
#include <SushiRuntime/usm_guard/usm_shared.hpp>
#include <SushiRuntime/probe_tasks/order_recorder.hpp>
#include <SushiRuntime/runtime_assembly/context_factory.hpp>

using namespace SushiRuntime;

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

    /**
     * @brief Tests concurrent execution of vector-add on CPU and GPU.
     *
     * Submits independent workloads to CPU and GPU queues back-to-back without waiting in between, and verifies both produce correct results.
     */
    TEST(Large_AsyncExecutionTest, CPUGPUConcurrentVectorAdd)
    {
        size_t cpu_idx = 0;
        size_t gpu_idx = 0;
        if (!find_cpu_gpu_indices(cpu_idx, gpu_idx))
            GTEST_SKIP() << "Need both a CPU and a GPU SYCL device for concurrency test.";

        const auto context_owner = SushiRuntime::Test::make_context();
        RuntimeContext& context = *context_owner;
        sycl::queue& gpu_q = context.devices().queue(gpu_idx);
        sycl::queue& cpu_q = context.devices().queue(cpu_idx);

        ASSERT_TRUE(gpu_q.get_device().is_gpu());
        ASSERT_TRUE(cpu_q.get_device().is_cpu());

        constexpr size_t N = 1u << 16;

        USMShared<float> g_a(gpu_q, N), g_b(gpu_q, N), g_c(gpu_q, N);
        USMShared<float> c_a(cpu_q, N), c_b(cpu_q, N), c_c(cpu_q, N);
        ASSERT_TRUE(g_a);
        ASSERT_TRUE(g_b);
        ASSERT_TRUE(g_c);
        ASSERT_TRUE(c_a);
        ASSERT_TRUE(c_b);
        ASSERT_TRUE(c_c);

        for (size_t i = 0; i < N; ++i)
        {
            g_a[i] = float(i);
            g_b[i] = float(i) * 2.f;
            g_c[i] = 0.f;
            c_a[i] = float(i) + 1.f;
            c_b[i] = float(i) * 3.f;
            c_c[i] = 0.f;
        }

        float* ga = g_a.get();
        float* gb = g_b.get();
        float* gc = g_c.get();
        float* ca = c_a.get();
        float* cb = c_b.get();
        float* cc = c_c.get();

        // Neither submit waits on the other, so the two devices run in parallel; see README.md.
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

        bool gpu_ok = true;
        bool cpu_ok = true;
        for (size_t i = 0; i < N; ++i)
        {
            if (gc[i] != float(i) + float(i) * 2.f)
            {
                gpu_ok = false;
                break;
            }
        }
        for (size_t i = 0; i < N; ++i)
        {
            if (cc[i] != (float(i) + 1.f) + float(i) * 3.f)
            {
                cpu_ok = false;
                break;
            }
        }

        EXPECT_TRUE(gpu_ok) << "GPU half of concurrent execution produced wrong results.";
        EXPECT_TRUE(cpu_ok) << "CPU half of concurrent execution produced wrong results.";
    }

    /**
     * @brief Verifies that asynchronous GPU dispatch does not block the host.
     *
     * Submits a heavy kernel to the GPU queue and asserts that it remains in-flight immediately after the submit call returns.
     */
    TEST(Large_AsyncExecutionTest, AsyncDispatchDoesNotBlock)
    {
        size_t cpu_idx = 0;
        size_t gpu_idx = 0;
        if (!find_cpu_gpu_indices(cpu_idx, gpu_idx))
            GTEST_SKIP() << "Need a GPU SYCL device for async dispatch test.";

        const auto context_owner = SushiRuntime::Test::make_context();
        RuntimeContext& context = *context_owner;
        sycl::queue& gpu_q = context.devices().queue(gpu_idx);

        constexpr size_t N = 1u << 20;
        USMShared<float> in(gpu_q, N), out(gpu_q, N);
        ASSERT_TRUE(in);
        ASSERT_TRUE(out);
        for (size_t i = 0; i < N; ++i)
            in[i] = float(i % 97);
        float* pin  = in.get();
        float* pout = out.get();

        using status_t = sycl::info::event_command_status;

        bool observed_inflight = false;
        for (int attempt = 0; attempt < 5 && !observed_inflight; ++attempt)
        {
            sycl::event ev = gpu_q.submit([&](sycl::handler& h)
            {
                h.parallel_for(sycl::range<1>(N), [=](sycl::id<1> i)
                {
                    float acc = pin[i];
                    // Heavy per-item loop so the host can observe it still running.
                    for (int k = 0; k < 4096; ++k)
                        acc = acc * 1.0000001f + 0.5f;
                    pout[i] = acc;
                });
            });

            if (ev.get_info<sycl::info::event::command_execution_status>() != status_t::complete)
                observed_inflight = true;

            ev.wait_and_throw();
        }

        EXPECT_TRUE(observed_inflight)
            << "GPU submit appears to block until completion — no async dispatch observed.";
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

        bool ok = true;
        for (size_t i = 0; i < N; ++i)
        {
            const float fi = float(i);
            if (std::abs(b[i] - (fi * 2.0f)) > 1e-3f)
            {
                ok = false;
                break;
            }
            if (std::abs(c[i] - (fi + 1.0f)) > 1e-3f)
            {
                ok = false;
                break;
            }
            if (std::abs(d[i] - (3.0f * fi + 1.0f)) > 1e-3f)
            {
                ok = false;
                break;
            }
        }
        EXPECT_TRUE(ok) << "Engine-driven device-kernel diamond produced incorrect results.";
    }
} // namespace DevicePipelineTests

/** @brief Tests many independent host, device, host chains running at once through the scheduler. */
namespace HeterogeneousPipelineTests
{
    using namespace SushiRuntime::Internal::Execution;
    using namespace SushiRuntime::Internal::Graph;
    using SushiRuntime::Test::USMShared;

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

        constexpr int    CHAINS = 16;
        constexpr size_t N      = 2048;
        constexpr float  SCALE  = 3.0f;
        constexpr float  BIAS   = 5.0f;

        // Queues come from the context's deduplicated set, which get_devices() can exceed; see README.md.
        std::vector<sycl::queue*> device_queues;
        for (size_t i = 0; i < context.devices().count(); ++i)
        {
            sycl::queue& queue = context.devices().queue(i);
            if (queue.get_device().is_gpu() || queue.get_device().is_cpu())
                device_queues.push_back(&queue);
        }
        ASSERT_FALSE(device_queues.empty()) << "No CPU/GPU device queues available.";

        // Pin each chain to one device queue so its buffer and kernels share a context.
        std::vector<USMShared<float>> buffers;
        buffers.reserve(CHAINS);
        for (int c = 0; c < CHAINS; ++c)
        {
            sycl::queue& dq = *device_queues[static_cast<size_t>(c) % device_queues.size()];
            buffers.emplace_back(dq, N);
            ASSERT_TRUE(buffers.back()) << "USM allocation failed for chain " << c;
        }

        for (int c = 0; c < CHAINS; ++c)
        {
            float* data = buffers[c].get();
            sycl::queue* dq = device_queues[static_cast<size_t>(c) % device_queues.size()];
            const bool on_gpu = dq->get_device().is_gpu();

            if (on_gpu)
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
                    [data, dq](sycl::queue&,
                               const std::vector<sycl::event>& dependencies) -> sycl::event
                    {
                        return dq->submit([&](sycl::handler& h)
                        {
                            h.depends_on(dependencies);
                            h.parallel_for(sycl::range<1>(N), [=](sycl::id<1> i)
                            {
                                data[i] = data[i] * SCALE;
                            });
                        });
                    });

                TaskMetadata meta_bias{"chain_bias_dev"};
                graph.add_task(meta_bias, {SushiRuntime::BufferRegion(data)}, {SushiRuntime::BufferRegion(data)},
                    [data, dq](sycl::queue&,
                               const std::vector<sycl::event>& dependencies) -> sycl::event
                    {
                        return dq->submit([&](sycl::handler& h)
                        {
                            h.depends_on(dependencies);
                            h.parallel_for(sycl::range<1>(N), [=](sycl::id<1> i)
                            {
                                data[i] = data[i] + BIAS;
                            });
                        });
                    });
            }
            else
            {
                TaskMetadata meta_init{"chain_init_host"};
                graph.add_task(meta_init, {}, {SushiRuntime::BufferRegion(data)},
                    [data, c](sycl::queue&, const std::vector<sycl::event>&) -> sycl::event
                    {
                        for (size_t i = 0; i < N; ++i)
                            data[i] = float(i) + float(c);
                        return {};
                    });

                TaskMetadata meta_scale{"chain_scale_host"};
                graph.add_task(meta_scale, {SushiRuntime::BufferRegion(data)}, {SushiRuntime::BufferRegion(data)},
                    [data](sycl::queue&, const std::vector<sycl::event>&) -> sycl::event
                    {
                        for (size_t i = 0; i < N; ++i)
                            data[i] = data[i] * SCALE;
                        return {};
                    });

                TaskMetadata meta_bias{"chain_bias_host"};
                graph.add_task(meta_bias, {SushiRuntime::BufferRegion(data)}, {SushiRuntime::BufferRegion(data)},
                    [data](sycl::queue&, const std::vector<sycl::event>&) -> sycl::event
                    {
                        for (size_t i = 0; i < N; ++i)
                            data[i] = data[i] + BIAS;
                        return {};
                    });
            }
        }

        auto compiled = graph.compile().value();
        context.execution_engine().execute(std::move(compiled)).value();
        context.devices().wait_all().value();

        for (int c = 0; c < CHAINS; ++c)
        {
            const float* data = buffers[c].get();
            bool ok = true;
            for (size_t i = 0; i < N; ++i)
            {
                const float expected = (float(i) + float(c)) * SCALE + BIAS;
                if (std::abs(data[i] - expected) > 1e-3f)
                {
                    ok = false;
                    break;
                }
            }
            EXPECT_TRUE(ok) << "Chain " << c << " produced incorrect results.";
        }
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
            for (size_t i = 0; i < VECTOR_SIZE; ++i)
            {
                A[i] = static_cast<float>(i);
                B[i] = static_cast<float>(i * 2);
                C[i] = 0.0f;
            }
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

        bool passed = true;
        for (size_t i = 0; i < VECTOR_SIZE; ++i)
        {
            float expected = static_cast<float>(i) + static_cast<float>(i * 2);
            if (C[i] != expected)
            {
                passed = false;
                break;
            }
        }

        EXPECT_TRUE(passed) << "Vector addition kernel produced incorrect results!";
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

        EXPECT_NEAR(*sum, expected, expected * 1e-6)
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
    } // namespace

    TEST(Medium_ReplayHostWrite, AReplayReadsASharedWordTheHostWrote)
    {
        const auto context_owner = SushiRuntime::Test::make_context();
        RuntimeContext& context = *context_owner;
        for (std::size_t device = 0; device < context.devices().count(); ++device)
            replay_around_host_writes(context, USMKind::Shared, device);
    }

    TEST(Medium_ReplayHostWrite, AReplayReadsAHostWordTheHostWrote)
    {
        const auto context_owner = SushiRuntime::Test::make_context();
        RuntimeContext& context = *context_owner;
        for (std::size_t device = 0; device < context.devices().count(); ++device)
            replay_around_host_writes(context, USMKind::Host, device);
    }
} // namespace ReplayHostWriteTests

/** @brief Tests TaskGraph ordering for a sequential dependency, independent roots and a diamond. */
namespace GraphTests
{
    using namespace SushiRuntime::Internal::Execution;
    using namespace SushiRuntime::Internal::Graph;
    using SushiRuntime::Test::make_order_recorder;

    /**
     * @brief Tests sequential execution due to USM dependencies.
     *
     * Three tasks sharing one pointer must run strictly in order (RAW, then WAR/WAW).
     */
    TEST(Medium_TaskGraphTest, SequentialUSMDependency)
    {
        const auto ctx_owner = SushiRuntime::Test::make_context();
        RuntimeContext& ctx = *ctx_owner;
        TaskGraph graph;

        int dummy_data = 0;
        void* usm_ptr = &dummy_data;

        std::vector<int> execution_order;
        std::mutex order_mutex;

        constexpr auto delay = std::chrono::milliseconds(5);

        TaskMetadata meta1{"Writer1"};
        graph.add_task(meta1, {}, {SushiRuntime::BufferRegion(usm_ptr)}, make_order_recorder(1, execution_order, order_mutex, delay));

        TaskMetadata meta2{"Reader1"};
        graph.add_task(meta2, {SushiRuntime::BufferRegion(usm_ptr)}, {}, make_order_recorder(2, execution_order, order_mutex, delay));

        TaskMetadata meta3{"Writer2"};
        graph.add_task(meta3, {}, {SushiRuntime::BufferRegion(usm_ptr)}, make_order_recorder(3, execution_order, order_mutex, delay));

        auto compiled_graph = graph.compile().value();
        EXPECT_EQ(compiled_graph.roots.size(), 1) << "Graph should only have 1 root due to linear dependency.";

        ctx.execution_engine().execute(std::move(compiled_graph)).value();

        std::lock_guard<std::mutex> lock(order_mutex);
        ASSERT_EQ(execution_order.size(), 3);
        EXPECT_EQ(execution_order[0], 1);
        EXPECT_EQ(execution_order[1], 2);
        EXPECT_EQ(execution_order[2], 3);
    }

    /**
     * @brief Tests concurrent execution of independent tasks.
     *
     * Tasks with no shared memory are independent roots and execute concurrently.
     */
    TEST(Medium_TaskGraphTest, IndependentRootsExecution)
    {
        const auto ctx_owner = SushiRuntime::Test::make_context();
        RuntimeContext& ctx = *ctx_owner;
        TaskGraph graph;

        constexpr int NUM_TASKS = 10;
        std::atomic<int> completed_tasks{0};

        auto host_task = [&completed_tasks](sycl::queue&, const std::vector<sycl::event>&) -> sycl::event
        {
            completed_tasks.fetch_add(1, std::memory_order_relaxed);
            return {};
        };

        for (int i = 0; i < NUM_TASKS; ++i)
        {
            TaskMetadata meta{"IndependentNode"};
            graph.add_task(meta, {}, {}, host_task);
        }

        auto compiled_graph = graph.compile().value();

        EXPECT_EQ(compiled_graph.roots.size(), NUM_TASKS);

        ctx.execution_engine().execute(std::move(compiled_graph)).value();

        EXPECT_EQ(completed_tasks.load(), NUM_TASKS);
    }

    /**
     * @brief Tests execution of a diamond DAG.
     *
     * A(write X) -> B,C(read X) -> D(write X). B and C run in parallel after A; D waits for both resolving the WAR hazard.
     */
    TEST(Medium_TaskGraphTest, FanOutFanInDiamond)
    {
        const auto ctx_owner = SushiRuntime::Test::make_context();
        RuntimeContext& ctx = *ctx_owner;
        TaskGraph graph;

        int shared_resource = 0;
        void* ptr = &shared_resource;

        std::atomic<int> current_phase{0};
        std::atomic<bool> fan_out_success{true};

        TaskMetadata metaA{"Node_A"};
        graph.add_task(metaA, {}, {SushiRuntime::BufferRegion(ptr)}, [&current_phase](sycl::queue&, const std::vector<sycl::event>&) -> sycl::event
        {
            current_phase.store(1, std::memory_order_release);
            return {};
        });

        auto reader_task = [&current_phase, &fan_out_success](sycl::queue&, const std::vector<sycl::event>&) -> sycl::event
        {
            if (current_phase.load(std::memory_order_acquire) < 1)
                fan_out_success = false;

            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            return {};
        };

        TaskMetadata metaB{"Node_B"};
        TaskMetadata metaC{"Node_C"};
        graph.add_task(metaB, {SushiRuntime::BufferRegion(ptr)}, {}, reader_task);
        graph.add_task(metaC, {SushiRuntime::BufferRegion(ptr)}, {}, reader_task);

        TaskMetadata metaD{"Node_D"};
        graph.add_task(metaD, {}, {SushiRuntime::BufferRegion(ptr)}, [&current_phase](sycl::queue&, const std::vector<sycl::event>&) -> sycl::event
        {
            current_phase.store(3, std::memory_order_release);
            return {};
        });

        auto compiled = graph.compile().value();
        ctx.execution_engine().execute(std::move(compiled)).value();

        EXPECT_TRUE(fan_out_success) << "Readers executed before the initial writer completed!";
        EXPECT_EQ(current_phase.load(), 3) << "Final writer did not complete successfully.";
    }
} // namespace GraphTests
