/****************************************************************/
/* test_async_overlap_perf.cpp                                  */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

// Quantifies CPU/GPU concurrency: dispatched without an intervening wait, the
// two devices run together so wall-clock approaches max(T_cpu, T_gpu) instead of
// the serial sum. Overlap only shows when runtimes are comparable, so the GPU
// iteration count is auto-calibrated to the CPU first. This measures DEVICE
// QUEUE overlap only — not the scheduler, not H2D/D2H transfer.

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>

#include "../../common/test_helpers.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Execution;
using SushiRuntime::Test::UsmShared;

namespace
{
    using Clock = std::chrono::steady_clock;

    /**
     * @brief Check if running on a WSL2 host.
     * 
     * Its CUDA backend adds dispatch latency and exposes no PCI topology, inflating the
     * overlap wall-clock enough to miss the strict bar even when the devices demonstrably
     * overlap — used to relax, not delete.
     * 
     * @return True if running under WSL, false otherwise.
     */
    bool running_under_wsl()
    {
#if defined(__linux__)
        std::ifstream f("/proc/version");
        if (!f)
            return false;
        std::string v((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        return v.find("microsoft") != std::string::npos ||
               v.find("Microsoft") != std::string::npos ||
               v.find("WSL") != std::string::npos;
#else
        return false;
#endif
    }

    /**
     * @brief Calculate milliseconds since a given time point.
     * 
     * @param t0 The start time.
     * @return Milliseconds elapsed.
     */
    double ms_since(Clock::time_point t0)
    {
        return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    }

    /**
     * @brief Run a function multiple times and keep the fastest time.
     * 
     * This strips scheduling/OS noise.
     * 
     * @tparam Fn The function type.
     * @param reps Number of repetitions.
     * @param fn The function to run.
     * @return The best execution time in milliseconds.
     */
    template <typename Fn>
    double best_of(int reps, Fn&& fn)
    {
        double best = 1e30;
        for (int r = 0; r < reps; ++r)
        {
            auto t0 = Clock::now();
            fn();
            best = std::min(best, ms_since(t0));
        }
        return best;
    }

    /**
     * @brief Find the indices for a CPU and GPU device.
     * 
     * @param cpu_idx Reference to store the CPU index.
     * @param gpu_idx Reference to store the GPU index.
     * @return True if both were found, false otherwise.
     */
    bool find_cpu_gpu_indices(size_t& cpu_idx, size_t& gpu_idx)
    {
        auto devs = sycl::device::get_devices(sycl::info::device_type::all);
        bool fc = false;
        bool fg = false;
        for (size_t i = 0; i < devs.size(); ++i)
        {
            if (!fc && devs[i].is_cpu())
            {
                cpu_idx = i;
                fc = true;
            }
            if (!fg && devs[i].is_gpu())
            {
                gpu_idx = i;
                fg = true;
            }
        }
        return fc && fg;
    }

    /**
     * @brief Launch a heavy, optimizer-resistant element kernel.
     * 
     * Runtime scales with `iters`. The body must be a genuinely non-linear recurrence
     * to prevent compiler optimizations from collapsing the loop.
     * 
     * @param q The SYCL queue.
     * @param in The input buffer.
     * @param out The output buffer.
     * @param n The number of elements.
     * @param iters Number of iterations per element.
     * @return The SYCL event for the kernel.
     */
    sycl::event launch_heavy(sycl::queue& q, float* in, float* out, size_t n, int iters)
    {
        return q.submit([&](sycl::handler& h)
        {
            h.parallel_for(sycl::range<1>(n), [=](sycl::id<1> i)
            {
                float acc = in[i];
                for (int k = 0; k < iters; ++k)
                    acc = sycl::sin(acc) * 1.0000001f + 0.5f;
                out[i] = acc;
            });
        });
    }
}

/**
 * @brief Tests the CPU/GPU async overlap speedup.
 * 
 * Auto-balances CPU/GPU work, then measures the wall-clock saving of running the
 * two devices concurrently versus serially.
 */
TEST(Performance_AsyncOverlapBench, CpuGpuOverlapSpeedup)
{
    size_t cpu_idx = 0;
    size_t gpu_idx = 0;
    if (!find_cpu_gpu_indices(cpu_idx, gpu_idx))
        GTEST_SKIP() << "Need both a CPU and a GPU SYCL device for overlap benchmark.";

    RuntimeContext ctx;
    sycl::queue& gpu_q = ctx.get_queue(gpu_idx);
    sycl::queue& cpu_q = ctx.get_queue(cpu_idx);

    constexpr size_t N = 1u << 20; // 1M items per device
    constexpr int CPU_ITERS = 2048;
    int gpu_iters = 2048; // calibrated below

    UsmShared<float> g_in(gpu_q, N), g_out(gpu_q, N);
    UsmShared<float> c_in(cpu_q, N), c_out(cpu_q, N);
    ASSERT_TRUE(g_in);
    ASSERT_TRUE(g_out);
    ASSERT_TRUE(c_in);
    ASSERT_TRUE(c_out);

    for (size_t i = 0; i < N; ++i)
    {
        g_in[i] = float(i % 97);
        c_in[i] = float(i % 89);
    }
    float* gi = g_in.get();
    float* go = g_out.get();
    float* ci = c_in.get();
    float* co = c_out.get();

    // Warmup (JIT compile + first-touch paging) before any timing.
    launch_heavy(gpu_q, gi, go, N, gpu_iters).wait();
    launch_heavy(cpu_q, ci, co, N, CPU_ITERS).wait();

    // Calibration: scale GPU iters so its runtime matches the CPU kernel.
    const double cpu_base = best_of(3, [&] { launch_heavy(cpu_q, ci, co, N, CPU_ITERS).wait(); });
    const double gpu_base = best_of(3, [&] { launch_heavy(gpu_q, gi, go, N, gpu_iters).wait(); });

    if (gpu_base > 1e-3)
    {
        const double ratio = cpu_base / gpu_base;
        gpu_iters = std::max(1, static_cast<int>(gpu_iters * ratio));
    }
    launch_heavy(gpu_q, gi, go, N, gpu_iters).wait(); // re-warm at calibrated size

    // Isolated, balanced device timings.
    const double t_gpu = best_of(3, [&] { launch_heavy(gpu_q, gi, go, N, gpu_iters).wait(); });
    const double t_cpu = best_of(3, [&] { launch_heavy(cpu_q, ci, co, N, CPU_ITERS).wait(); });

    // Serial: GPU fully, then CPU fully.
    const double t_serial = best_of(3, [&]
    {
        launch_heavy(gpu_q, gi, go, N, gpu_iters).wait();
        launch_heavy(cpu_q, ci, co, N, CPU_ITERS).wait();
    });

    // Overlap: dispatch both, then join.
    const double t_overlap = best_of(3, [&]
    {
        sycl::event ge = launch_heavy(gpu_q, gi, go, N, gpu_iters);
        sycl::event ce = launch_heavy(cpu_q, ci, co, N, CPU_ITERS);
        ge.wait();
        ce.wait();
    });

    // Keep results alive so the compiler can't elide the kernels.
    volatile float sink = go[0] + co[0];
    (void)sink;

    const double ideal   = std::max(t_gpu, t_cpu);
    const double speedup = t_serial / t_overlap;

    std::printf("\n=== CPU/GPU async overlap (N=%zu, gpu_iters=%d, cpu_iters=%d) ===\n",
                N, gpu_iters, CPU_ITERS);
    std::printf("  GPU alone     : %8.2f ms\n", t_gpu);
    std::printf("  CPU alone     : %8.2f ms\n", t_cpu);
    std::printf("  Serial (G+C)  : %8.2f ms\n", t_serial);
    std::printf("  Overlap       : %8.2f ms\n", t_overlap);
    std::printf("  Ideal max(G,C): %8.2f ms\n", ideal);
    std::printf("  Speedup       : %8.2fx\n", speedup);

    // 0.75 leaves slack for dispatch overhead and contention while still failing
    // if work serializes (~1.0x).
    const double overlap_bar       = t_serial * 0.75;  // healthy concurrency
    const double serialization_top = t_serial * 0.95;  // essentially no overlap

    // A genuine serialization bug is a hard failure everywhere (also catches
    // CUDA_LAUNCH_BLOCKING=1, where every launch runs synchronously).
    EXPECT_LT(t_overlap, serialization_top)
        << "CPU and GPU are not overlapping at all — work is serializing "
           "(check that CUDA_LAUNCH_BLOCKING is not set to 1).";

    if (t_overlap >= overlap_bar && t_overlap < serialization_top && running_under_wsl())
    {
        // Overlap is clearly happening but doesn't clear the strict bar; on
        // WSL2 + Pascal the dispatch overhead inflates it. Inconclusive here;
        // bare-metal CI still enforces the bar.
        GTEST_SKIP() << "Overlap present (" << speedup << "x) but below the 0.75 bar; "
                        "async overlap is not reliably measurable on WSL2/Pascal.";
    }

    EXPECT_LT(t_overlap, overlap_bar)
        << "CPU and GPU are not overlapping as expected — work may be serializing.";
}
