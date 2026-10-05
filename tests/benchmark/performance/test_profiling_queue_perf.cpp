/****************************************************************/
/* test_profiling_queue_perf.cpp                                */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

/**
 * @file test_profiling_queue_perf.cpp
 * @brief Measures the per-submission cost a profiling queue adds over the plain queue.
 * @author Mustafa Garip
 * @see tests/benchmark/README.md
 */

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <sycl/sycl.hpp>
#include <gtest/gtest.h>

#include <SushiRuntime/execution/runtime_context.hpp>

using namespace SushiRuntime::Execution;

namespace
{
    constexpr std::size_t SUBMISSIONS = 200;  // per timed round
    constexpr int         ROUNDS      = 3;    // best-of-N to strip jitter
    constexpr std::size_t RANGE       = 4096; // work-items per submission

    /**
     * @brief Submits SUBMISSIONS trivial kernels to @p q and waits once at the end.
     * @param q Queue to submit to.
     * @param data USM buffer the kernel writes; sized at least RANGE.
     * @return Wall time in milliseconds for the whole round.
     */
    double time_round(sycl::queue& q, int* data)
    {
        const auto t0 = std::chrono::steady_clock::now();
        for (std::size_t i = 0; i < SUBMISSIONS; ++i)
        {
            q.submit([&](sycl::handler& h)
            {
                h.parallel_for(sycl::range<1>(RANGE), [=](sycl::id<1> idx)
                {
                    data[idx] = static_cast<int>(idx[0]) * 2 + 1;
                });
            });
        }
        q.wait();
        return std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t0).count();
    }
}

/**
 * @brief Measures the per-submission cost a profiling-enabled queue adds over the plain queue.
 */
TEST(Performance_ProfilingQueueBench, ProfilingQueueDeltaOverPlainQueue)
{
    RuntimeContext ctx;
    ASSERT_GT(ctx.device_count(), 0u) << "no device to benchmark on";

    sycl::queue& plain = ctx.get_queue(0);
    sycl::queue& profiling = ctx.profiling_queue(0);
    ASSERT_EQ(profiling.get_context(), plain.get_context())
        << "the two queues must share a context for the comparison to be fair";

    int* plain_data = sycl::malloc_shared<int>(RANGE, plain);
    int* profiling_data = sycl::malloc_shared<int>(RANGE, plain);
    ASSERT_NE(plain_data, nullptr);
    ASSERT_NE(profiling_data, nullptr);

    // The warm-up is untimed, so first-submission driver and JIT costs reach neither queue.
    time_round(plain, plain_data);
    time_round(profiling, profiling_data);

    double best_plain_ms = 1e300;
    double best_profiling_ms = 1e300;
    for (int r = 0; r < ROUNDS; ++r)
    {
        // The first queue alternates each round, so thermal or driver drift favours neither.
        const bool plain_first = (r % 2) == 0;
        double plain_ms, profiling_ms;
        if (plain_first)
        {
            plain_ms = time_round(plain, plain_data);
            profiling_ms = time_round(profiling, profiling_data);
        }
        else
        {
            profiling_ms = time_round(profiling, profiling_data);
            plain_ms = time_round(plain, plain_data);
        }
        best_plain_ms = std::min(best_plain_ms, plain_ms);
        best_profiling_ms = std::min(best_profiling_ms, profiling_ms);
    }

    for (std::size_t i = 0; i < RANGE; ++i)
        ASSERT_EQ(plain_data[i], profiling_data[i])
            << "the two queues must compute the identical result at i=" << i;

    const double plain_us_per_submit = (best_plain_ms * 1000.0) / double(SUBMISSIONS);
    const double profiling_us_per_submit = (best_profiling_ms * 1000.0) / double(SUBMISSIONS);
    const double delta_us_per_submit = profiling_us_per_submit - plain_us_per_submit;

    std::printf("\n=== Profiling queue submission overhead (%zu submissions/round, %d rounds) ===\n",
                SUBMISSIONS, ROUNDS);
    std::printf("  Plain queue      : %8.2f us/submit\n", plain_us_per_submit);
    std::printf("  Profiling queue  : %8.2f us/submit\n", profiling_us_per_submit);
    std::printf("  Delta            : %8.2f us/submit (measured ~24 us on RTX 3080 Ti/CUDA)\n",
                delta_us_per_submit);

    sycl::free(plain_data, plain);
    sycl::free(profiling_data, plain);

    EXPECT_GT(best_plain_ms, 0.0);
    EXPECT_GT(best_profiling_ms, 0.0);
}
