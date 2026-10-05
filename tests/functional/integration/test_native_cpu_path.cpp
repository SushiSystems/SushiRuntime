/****************************************************************/
/* test_native_cpu_path.cpp                                     */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

/**
 * @file test_native_cpu_path.cpp
 * @brief Tests add_host(), which runs plain C++ on a worker thread ordered by the data it touches.
 * @see tests/functional/integration/README.md
 * @author Mustafa Garip
 */

#include <atomic>
#include <cstddef>
#include <stdexcept>
#include <gtest/gtest.h>
#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

/** @brief Tests that a host task ordered after a device kernel sees the kernel's output. */
TEST(Integration_NativeCPU, HostTaskRunsAfterKernel)
{
    constexpr std::size_t N = 64;

    auto rt = Runtime::create();
    auto a  = rt.buffer<int>(N);
    auto b  = rt.buffer<int>(N);
    for (std::size_t i = 0; i < N; ++i) { a[i] = 0; b[i] = 0; }

    int* ap = a.data();
    int* bp = b.data();

    auto g = rt.graph();
    g.add(API::Reads{}, API::Writes(a), N,
          [ap](std::size_t i) { ap[i] = static_cast<int>(i) * 2; });
    g.add_host(API::Reads(a), API::Writes(b),
               [ap, bp]() { for (std::size_t i = 0; i < N; ++i) bp[i] = ap[i] + 1; });

    g.run();

    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(b[i], static_cast<int>(i) * 2 + 1) << "at i=" << i;
}

/** @brief Tests that a device kernel ordered after a host task sees the host's writes. */
TEST(Integration_NativeCPU, KernelRunsAfterHostTask)
{
    constexpr std::size_t N = 64;

    auto rt = Runtime::create();
    auto a  = rt.buffer<int>(N);
    auto b  = rt.buffer<int>(N);
    for (std::size_t i = 0; i < N; ++i) { a[i] = 0; b[i] = 0; }

    int* ap = a.data();
    int* bp = b.data();

    auto g = rt.graph();
    g.add_host(API::Reads{}, API::Writes(a),
               [ap]() { for (std::size_t i = 0; i < N; ++i) ap[i] = static_cast<int>(i) + 5; });
    g.add(API::Reads(a), API::Writes(b), N,
          [ap, bp](std::size_t i) { bp[i] = ap[i] * 10; });

    g.run();

    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(b[i], (static_cast<int>(i) + 5) * 10) << "at i=" << i;
}

/** @brief Tests that an untracked host task runs once per step. */
TEST(Integration_NativeCPU, UntrackedHostTaskRunsEveryStep)
{
    constexpr std::size_t STEPS = 5;

    auto rt = Runtime::create();
    std::atomic<int> ticks{0};

    auto g = rt.graph();
    g.add_host([&ticks]() { ticks.fetch_add(1, std::memory_order_relaxed); });

    g.run(STEPS);

    EXPECT_EQ(ticks.load(), static_cast<int>(STEPS));
}

/** @brief Tests that a throwing host task fails the run and leaves the runtime usable. */
TEST(Integration_NativeCPU, ThrowingHostTaskKeepsRuntimeUsable)
{
    auto rt = Runtime::create();

    auto bad = rt.graph();
    bad.add_host([]() { throw std::runtime_error("boom"); });
    auto r1 = bad.try_run();
    ASSERT_FALSE(r1.has_value()) << "a throwing native task must fail the run.";

    constexpr std::size_t N = 16;
    auto buf = rt.buffer<int>(N);
    for (std::size_t i = 0; i < N; ++i) buf[i] = 0;
    int* p = buf.data();

    auto good = rt.graph();
    good.add_host(API::Reads{}, API::Writes(buf),
                  [p]() { for (std::size_t i = 0; i < N; ++i) p[i] = static_cast<int>(i); });
    auto r2 = good.try_run();
    ASSERT_TRUE(r2.has_value()) << "the pool must stay re-armable after a failure.";

    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(buf[i], static_cast<int>(i)) << "at i=" << i;
}
