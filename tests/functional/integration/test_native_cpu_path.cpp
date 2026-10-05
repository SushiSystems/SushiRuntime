/****************************************************************/
/* test_native_cpu_path.cpp                                     */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

// WP-7 coverage: the native CPU execution path. add_host() runs plain C++ on a
// worker thread with no SYCL submission, ordered into the graph by the data it
// touches, and shares one completion contract with kernel tasks.

#include <atomic>
#include <cstddef>
#include <stdexcept>
#include <gtest/gtest.h>
#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

// A native host task ordered after a device kernel must see the kernel's output:
// it is dispatched only once the kernel's event completed, so the shared USM the
// kernel wrote is visible host-side. Ordering comes from the shared buffer a.
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

// A device kernel ordered after a native host task must see the host's writes:
// the kernel waits on the host task's (inline) completion before dispatch, and
// shared USM written on the host is visible on the device.
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

// An untracked native task runs once per step, proving the dependency-free form
// is both a root and a leaf and replays each step.
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

// A throwing native task fails the run (WP-0 error isolation), and the runtime
// stays usable: a fresh graph runs cleanly afterward, proving native tasks share
// the kernel error path rather than latching the worker pool dead.
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
