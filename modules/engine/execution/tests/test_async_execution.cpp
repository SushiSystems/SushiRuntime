/**************************************************************************/
/* test_async_execution.cpp                                               */
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
 * @file test_async_execution.cpp
 * @brief Tests that CPU and GPU work on per-device queues runs concurrently and race-free.
 * @see modules/engine/execution/tests/README.md
 * @author Mustafa Garip
 */

#include <vector>
#include <gtest/gtest.h>
#include <sycl/sycl.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>

#include "test_helpers.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Execution;
using SushiRuntime::Test::UsmShared;

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
TEST(Integration_AsyncExecutionTest, CpuGpuConcurrentVectorAdd)
{
    size_t cpu_idx = 0;
    size_t gpu_idx = 0;
    if (!find_cpu_gpu_indices(cpu_idx, gpu_idx))
        GTEST_SKIP() << "Need both a CPU and a GPU SYCL device for concurrency test.";

    RuntimeContext ctx;
    sycl::queue& gpu_q = ctx.devices().queue(gpu_idx);
    sycl::queue& cpu_q = ctx.devices().queue(cpu_idx);

    ASSERT_TRUE(gpu_q.get_device().is_gpu());
    ASSERT_TRUE(cpu_q.get_device().is_cpu());

    constexpr size_t N = 1u << 16;

    UsmShared<float> g_a(gpu_q, N), g_b(gpu_q, N), g_c(gpu_q, N);
    UsmShared<float> c_a(cpu_q, N), c_b(cpu_q, N), c_c(cpu_q, N);
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
TEST(Integration_AsyncExecutionTest, AsyncDispatchDoesNotBlock)
{
    size_t cpu_idx = 0;
    size_t gpu_idx = 0;
    if (!find_cpu_gpu_indices(cpu_idx, gpu_idx))
        GTEST_SKIP() << "Need a GPU SYCL device for async dispatch test.";

    RuntimeContext ctx;
    sycl::queue& gpu_q = ctx.devices().queue(gpu_idx);

    constexpr size_t N = 1u << 20;
    UsmShared<float> in(gpu_q, N), out(gpu_q, N);
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
