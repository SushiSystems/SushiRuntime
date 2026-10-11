/**************************************************************************/
/* test_steady_state_allocation.cpp                                       */
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
 * @file test_steady_state_allocation.cpp
 * @brief Tests that a tick's calling-thread allocations do not scale beyond bare SYCL's own.
 * @see tests/allocation/README.md
 * @author Mustafa Garip
 */

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <string>

#include <gtest/gtest.h>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/device/queue_flavour.hpp>

using namespace SushiRuntime;

// Each sanitizer is detected by __has_feature and by its __SANITIZE_ macro; see README.md.
#if defined(__has_feature)
#  if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#    define SR_SANITIZER_OWNS_ALLOCATOR 1
#  endif
#endif
#if !defined(SR_SANITIZER_OWNS_ALLOCATOR)
#  if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#    define SR_SANITIZER_OWNS_ALLOCATOR 1
#  endif
#endif

#if defined(SR_SANITIZER_OWNS_ALLOCATOR)

/** @brief Skips the suite in a build whose sanitizer owns operator new. */
TEST(Integration_SteadyStateAllocation, SkippedUnderSanitizer)
{
    GTEST_SKIP() << "This sanitizer owns operator new; the allocation counter "
                    "cannot coexist with it. Run this suite in an ordinary build.";
}

#else

namespace
{
    // Both variables are constant-initialized, live before dynamic initialization; see README.md.
    thread_local bool g_counting_this_thread = false;
    std::atomic<std::size_t> g_allocation_count{0};

    // Counts an allocation when the calling thread asked to be observed.
    inline void note_allocation() noexcept
    {
        if (g_counting_this_thread)
            g_allocation_count.fetch_add(1, std::memory_order_relaxed);
    }

    constexpr std::size_t N            = 4096;
    constexpr std::size_t WARMUP_TICKS = 8;
    constexpr std::size_t TICKS        = 64;
}

// Each replaced form has its matching delete, and aligned forms stay default; see README.md.
void* operator new(std::size_t size)
{
    note_allocation();
    void* p = std::malloc(size != 0 ? size : 1);
    if (p == nullptr)
        throw std::bad_alloc();
    return p;
}

void* operator new[](std::size_t size)
{
    note_allocation();
    void* p = std::malloc(size != 0 ? size : 1);
    if (p == nullptr)
        throw std::bad_alloc();
    return p;
}

void operator delete(void* p) noexcept              { std::free(p); }
void operator delete[](void* p) noexcept            { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace
{
    /**
     * @brief Runs the given number of ticks after a warm-up and returns the allocations the calling
     *        thread made.
     */
    std::size_t allocations_for_reuse_ticks(Graph& g, std::size_t ticks)
    {
        Internal::Observability::RunReport report;
        for (std::size_t i = 0; i < WARMUP_TICKS; ++i)
            g.run(report);

        g_allocation_count.store(0, std::memory_order_relaxed);
        g_counting_this_thread = true;
        for (std::size_t i = 0; i < ticks; ++i)
            g.run(report);
        g_counting_this_thread = false;

        return g_allocation_count.load(std::memory_order_relaxed);
    }

    // The same, through the value-returning run() — one fresh RunReport per tick.
    std::size_t allocations_for_value_ticks(Graph& g, std::size_t ticks)
    {
        for (std::size_t i = 0; i < WARMUP_TICKS; ++i)
            g.run();

        g_allocation_count.store(0, std::memory_order_relaxed);
        g_counting_this_thread = true;
        for (std::size_t i = 0; i < ticks; ++i)
        {
            const Internal::Observability::RunReport report = g.run();
            (void)report;
        }
        g_counting_this_thread = false;

        return g_allocation_count.load(std::memory_order_relaxed);
    }

    /**
     * @brief Returns the allocations bare SYCL makes on this thread submitting the same kernel on
     *        an in-order queue.
     */
    template <class F>
    std::size_t allocations_for_baseline_ticks(sycl::queue& queue, std::size_t n, F fn,
                                               std::size_t ticks)
    {
        const auto submit_once = [&]
        {
            queue.submit([&](sycl::handler& h)
            {
                h.parallel_for(sycl::range<1>(n), [fn](sycl::id<1> idx) { fn(idx[0]); });
            });
            queue.wait();
        };

        for (std::size_t i = 0; i < WARMUP_TICKS; ++i)
            submit_once();

        g_allocation_count.store(0, std::memory_order_relaxed);
        g_counting_this_thread = true;
        for (std::size_t i = 0; i < ticks; ++i)
            submit_once();
        g_counting_this_thread = false;

        return g_allocation_count.load(std::memory_order_relaxed);
    }
}

/** @brief Tests that the counter observes an allocation on the calling thread. */
TEST(Integration_SteadyStateAllocation, TheCounterObservesTheCallingThread)
{
    g_allocation_count.store(0, std::memory_order_relaxed);
    g_counting_this_thread = true;
    {
        std::string forced(256, 'x');
        EXPECT_FALSE(forced.empty());
    }
    g_counting_this_thread = false;

    EXPECT_GT(g_allocation_count.load(std::memory_order_relaxed), 0u)
        << "the replacement operator new is not being called; every allocation "
           "claim in this file would pass vacuously";
}

// The four properties the header comment describes.
TEST(Integration_SteadyStateAllocation, ReuseTicksDoNotAllocatePerTick)
{
    const auto kernel = [](float* p) { return [p](std::size_t i) { p[i] = static_cast<float>(i) * 0.5f; }; };

    auto rt  = Runtime::create().value();               // rebalancer off, profiling off, chain path
    auto buf = rt.buffer<float>(N).value();
    for (std::size_t i = 0; i < N; ++i)
        buf[i] = 0.0f;

    float* p = buf.data();
    auto g = rt.graph();
    g.add(N, Reads{}, Writes(buf), kernel(p));

    const std::size_t reuse_short = allocations_for_reuse_ticks(g, TICKS);
    const std::size_t reuse_long  = allocations_for_reuse_ticks(g, TICKS * 4);
    const std::size_t value_short = allocations_for_value_ticks(g, TICKS);

    // The baseline queue is the in-order queue the chain itself submits to; see README.md.
    sycl::queue& baseline_queue =
        rt.unsafe_context().devices().queue(0, Internal::Device::QueueFlavour{true, false});
    const std::size_t baseline_short = allocations_for_baseline_ticks(baseline_queue, N, kernel(p), TICKS);
    const std::size_t baseline_long  = allocations_for_baseline_ticks(baseline_queue, N, kernel(p), TICKS * 4);

    RecordProperty("reuse_allocations_per_" + std::to_string(TICKS) + "_ticks",
                   static_cast<int>(reuse_short));
    RecordProperty("reuse_allocations_per_" + std::to_string(TICKS * 4) + "_ticks",
                   static_cast<int>(reuse_long));
    RecordProperty("value_allocations_per_" + std::to_string(TICKS) + "_ticks",
                   static_cast<int>(value_short));
    RecordProperty("baseline_allocations_per_" + std::to_string(TICKS) + "_ticks",
                   static_cast<int>(baseline_short));
    RecordProperty("baseline_allocations_per_" + std::to_string(TICKS * 4) + "_ticks",
                   static_cast<int>(baseline_long));

    // Claim 1: the value form allocates at least once more per tick than reuse; see README.md.
    EXPECT_GE(value_short, reuse_short + TICKS)
        << "the value-returning run() did not cost a per-tick allocation more "
           "than run(report); either the counter does not see the run path or "
           "the reuse overload is not actually reusing";

    // Claim 2: the chain's per-tick growth does not exceed bare SYCL's; see README.md.
    const auto chain_growth    = static_cast<std::ptrdiff_t>(reuse_long) - static_cast<std::ptrdiff_t>(reuse_short);
    const auto baseline_growth = static_cast<std::ptrdiff_t>(baseline_long) - static_cast<std::ptrdiff_t>(baseline_short);
    EXPECT_LE(chain_growth, baseline_growth)
        << "the chain's per-tick allocation growth (" << reuse_short << " -> " << reuse_long
        << ") exceeds bare SYCL's growth submitting the same kernel on the same queue ("
        << baseline_short << " -> " << baseline_long << ")";

    // Claim 3: the scheduler submits from a worker, which this counter cannot see; see README.md.
    RuntimeConfig forced;
    forced.force_scheduler = true;
    auto scheduled_rt  = Runtime::create(forced).value();
    auto scheduled_buf = scheduled_rt.buffer<float>(N).value();
    for (std::size_t i = 0; i < N; ++i)
        scheduled_buf[i] = 0.0f;

    auto sg = scheduled_rt.graph();
    sg.add(N, Reads{}, Writes(scheduled_buf), kernel(scheduled_buf.data()));

    const std::size_t scheduler_short = allocations_for_reuse_ticks(sg, TICKS);
    const std::size_t scheduler_long  = allocations_for_reuse_ticks(sg, TICKS * 4);

    RecordProperty("scheduler_reuse_allocations_per_" + std::to_string(TICKS) + "_ticks",
                   static_cast<int>(scheduler_short));
    RecordProperty("scheduler_reuse_allocations_per_" + std::to_string(TICKS * 4) + "_ticks",
                   static_cast<int>(scheduler_long));

    EXPECT_TRUE(scheduler_long == 0 || scheduler_long < 4 * scheduler_short)
        << "a scheduler-forced run's steady-state allocation scales with tick count: "
        << scheduler_short << " over " << TICKS << " ticks, " << scheduler_long
        << " over " << (TICKS * 4);
}

/** @brief Tests the same allocation claims on the asynchronous path. */
TEST(Integration_SteadyStateAllocation, AsyncWaitIntoAReusedReportIsAmortised)
{
    auto rt  = Runtime::create().value();
    auto buf = rt.buffer<float>(N).value();
    for (std::size_t i = 0; i < N; ++i)
        buf[i] = 0.0f;

    float* p = buf.data();
    auto g = rt.graph();
    g.add(N, Reads{}, Writes(buf),
          [p](std::size_t i) { p[i] = static_cast<float>(i) * 0.25f; });

    Internal::Observability::RunReport report;
    for (std::size_t i = 0; i < WARMUP_TICKS; ++i)
    {
        auto step = g.run_async();
        step.wait(report);
    }

    const auto measure = [&](std::size_t ticks)
    {
        g_allocation_count.store(0, std::memory_order_relaxed);
        g_counting_this_thread = true;
        for (std::size_t i = 0; i < ticks; ++i)
        {
            auto step = g.run_async();
            step.wait(report);
        }
        g_counting_this_thread = false;
        return g_allocation_count.load(std::memory_order_relaxed);
    };

    const std::size_t shorter = measure(TICKS);
    const std::size_t longer  = measure(TICKS * 4);

    RecordProperty("async_allocations_per_" + std::to_string(TICKS) + "_ticks",
                   static_cast<int>(shorter));

    EXPECT_TRUE(longer == 0 || longer < 4 * shorter)
        << "an overlapped tick allocates per step: " << shorter << " over "
        << TICKS << " ticks, " << longer << " over " << (TICKS * 4);
}

/** @brief Tests that a profiled run's event ledger allocates only for a new shape. */
TEST(Integration_SteadyStateAllocation, ProfiledReuseTicksDoNotAllocatePerTick)
{
    const auto kernel = [](int* p) { return [p](std::size_t i) { p[i] = static_cast<int>(i); }; };

    auto rt  = Runtime::create().value();
    auto buf = rt.buffer<int>(N).value();
    int* p = buf.data();

    auto g = rt.graph();
    g.set_profiling(true);
    g.add(N, Reads{}, Writes(buf), kernel(p));

    const std::size_t reuse_short = allocations_for_reuse_ticks(g, TICKS);
    const std::size_t reuse_long  = allocations_for_reuse_ticks(g, TICKS * 4);

    // The baseline uses the profiling flavour of the chain's in-order queue; see README.md.
    sycl::queue& baseline_queue =
        rt.unsafe_context().devices().queue(0, Internal::Device::QueueFlavour{true, true});
    const std::size_t baseline_short = allocations_for_baseline_ticks(baseline_queue, N, kernel(p), TICKS);
    const std::size_t baseline_long  = allocations_for_baseline_ticks(baseline_queue, N, kernel(p), TICKS * 4);

    RecordProperty("profiled_reuse_allocations_per_" + std::to_string(TICKS) + "_ticks",
                   static_cast<int>(reuse_short));
    RecordProperty("profiled_reuse_allocations_per_" + std::to_string(TICKS * 4) + "_ticks",
                   static_cast<int>(reuse_long));
    RecordProperty("profiled_baseline_allocations_per_" + std::to_string(TICKS) + "_ticks",
                   static_cast<int>(baseline_short));
    RecordProperty("profiled_baseline_allocations_per_" + std::to_string(TICKS * 4) + "_ticks",
                   static_cast<int>(baseline_long));

    const auto chain_growth    = static_cast<std::ptrdiff_t>(reuse_long) - static_cast<std::ptrdiff_t>(reuse_short);
    const auto baseline_growth = static_cast<std::ptrdiff_t>(baseline_long) - static_cast<std::ptrdiff_t>(baseline_short);
    EXPECT_LE(chain_growth, baseline_growth)
        << "the profiled chain's per-tick allocation growth (" << reuse_short << " -> " << reuse_long
        << ") exceeds bare SYCL's growth submitting the same kernel on the same queue ("
        << baseline_short << " -> " << baseline_long << ")";

    RuntimeConfig forced;
    forced.force_scheduler = true;
    auto scheduled_rt  = Runtime::create(forced).value();
    auto scheduled_buf = scheduled_rt.buffer<int>(N).value();

    auto sg = scheduled_rt.graph();
    sg.set_profiling(true);
    sg.add(N, Reads{}, Writes(scheduled_buf), kernel(scheduled_buf.data()));

    const std::size_t scheduler_short = allocations_for_reuse_ticks(sg, TICKS);
    const std::size_t scheduler_long  = allocations_for_reuse_ticks(sg, TICKS * 4);

    RecordProperty("profiled_scheduler_allocations_per_" + std::to_string(TICKS) + "_ticks",
                   static_cast<int>(scheduler_short));
    RecordProperty("profiled_scheduler_allocations_per_" + std::to_string(TICKS * 4) + "_ticks",
                   static_cast<int>(scheduler_long));

    EXPECT_TRUE(scheduler_long == 0 || scheduler_long < 4 * scheduler_short)
        << "a scheduler-forced profiled run's steady-state allocation scales with tick count: "
        << scheduler_short << " over " << TICKS << " ticks, " << scheduler_long << " over "
        << (TICKS * 4);
}

#endif // !defined(SR_SANITIZER_OWNS_ALLOCATOR)
