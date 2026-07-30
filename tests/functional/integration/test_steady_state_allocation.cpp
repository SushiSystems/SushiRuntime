/**************************************************************************/
/* test_steady_state_allocation.cpp                                       */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

// Makes the steady-state allocation cost of a tick measurable, and then holds
// it to a shape rather than to a number.
//
// A frame loop runs the same compiled plan thousands of times. Anything it
// allocates per tick is a malloc on the simulation thread's critical path and a
// slow drift toward heap fragmentation, and neither shows up in a throughput
// benchmark that runs the graph once over a large input. The counter below is a
// replacement global operator new, gated by a THREAD-LOCAL flag so it observes
// only the thread driving the run: worker threads allocate for reasons of their
// own (event lists, SYCL submissions) and their noise would drown the signal.
//
// What is asserted is deliberately not "zero". Whether a library-internal
// allocation reaches this counter at all depends on the platform — a replacement
// operator new in the executable preempts a shared library's on ELF and does not
// on Windows — so an absolute count is not a portable claim. Two relative ones
// are, and they are the ones that matter:
//
//   1. The value-returning run() costs at least one more allocation per tick
//      than the reuse form. That is the RunReport itself, and it makes the
//      counter demonstrably able to see the path it is measuring.
//   2. The reuse form's cost does not scale with the number of ticks. A cost
//      that is genuinely per-tick would make a four-times-longer run allocate
//      four times as much; an amortised one would not.
//
// The raw per-tick figures are recorded as test properties, so the number is
// visible even where it cannot be asserted.

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <string>

#include <gtest/gtest.h>

#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

#if defined(__SANITIZE_ADDRESS__)

// AddressSanitizer replaces operator new/delete itself, to place its redzones and
// track ownership. A second replacement in this translation unit is not a
// measurement — it is a duplicate definition of the same symbols, and on Windows
// it is a duplicate-symbol link error rather than anything subtler. There is also
// nothing here worth having under ASan: this suite counts allocations, and the
// sanitizer's own bookkeeping changes what the counts mean.
TEST(Integration_SteadyStateAllocation, SkippedUnderAddressSanitizer)
{
    GTEST_SKIP() << "AddressSanitizer owns operator new; the allocation counter "
                    "cannot coexist with it. Run this suite in an ordinary build.";
}

#else

namespace
{
    // Constant-initialized, so these are live before any dynamic initialization
    // that might allocate. The flag is per-thread; the counter is shared only
    // because the counting thread is the only one that ever touches it.
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

// Every replaced form is paired with the matching deallocation form, so nothing
// allocated here can be freed by a different allocator. The over-aligned
// (std::align_val_t) forms are deliberately NOT replaced: leaving the whole
// aligned family at its default keeps that pairing intact too, at the cost of
// not counting aligned allocations — which the run path does not make.
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
    // Runs @p ticks steps and returns how many allocations the calling thread
    // made doing it. The graph is warmed first so compilation and first-run
    // capacity growth land outside the measured window — the question is what a
    // tick costs in the steady state, not what the first one costs.
    std::size_t allocations_for_reuse_ticks(API::Graph& g, std::size_t ticks)
    {
        Core::RunReport report;
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
    std::size_t allocations_for_value_ticks(API::Graph& g, std::size_t ticks)
    {
        for (std::size_t i = 0; i < WARMUP_TICKS; ++i)
            g.run();

        g_allocation_count.store(0, std::memory_order_relaxed);
        g_counting_this_thread = true;
        for (std::size_t i = 0; i < ticks; ++i)
        {
            const Core::RunReport report = g.run();
            (void)report;
        }
        g_counting_this_thread = false;

        return g_allocation_count.load(std::memory_order_relaxed);
    }
}

// The counter must be able to see the thing it is measuring before any claim
// made with it means anything. A single vector allocation is unmissable.
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

// The two properties the header comment describes.
TEST(Integration_SteadyStateAllocation, ReuseTicksDoNotAllocatePerTick)
{
    auto rt  = Runtime::create();               // rebalancer off, profiling off
    auto buf = rt.buffer<float>(N);
    for (std::size_t i = 0; i < N; ++i)
        buf[i] = 0.0f;

    float* p = buf.data();
    auto g = rt.graph();
    g.add(API::Reads{}, API::Writes(buf), N,
          [p](std::size_t i) { p[i] = static_cast<float>(i) * 0.5f; });

    const std::size_t reuse_short = allocations_for_reuse_ticks(g, TICKS);
    const std::size_t reuse_long  = allocations_for_reuse_ticks(g, TICKS * 4);
    const std::size_t value_short = allocations_for_value_ticks(g, TICKS);

    RecordProperty("reuse_allocations_per_" + std::to_string(TICKS) + "_ticks",
                   static_cast<int>(reuse_short));
    RecordProperty("reuse_allocations_per_" + std::to_string(TICKS * 4) + "_ticks",
                   static_cast<int>(reuse_long));
    RecordProperty("value_allocations_per_" + std::to_string(TICKS) + "_ticks",
                   static_cast<int>(value_short));

    // 1. The report the value form returns is built fresh every tick; the reuse
    //    form refills one the caller owns. The difference is at least one
    //    allocation per tick, and observing it proves the counter reaches the
    //    run path rather than only the test's own code.
    EXPECT_GE(value_short, reuse_short + TICKS)
        << "the value-returning run() did not cost a per-tick allocation more "
           "than run(report); either the counter does not see the run path or "
           "the reuse overload is not actually reusing";

    // 2. Four times the ticks must not cost four times the allocations. A cost
    //    that survives this is per-tick, which is what a frame loop cannot have.
    EXPECT_TRUE(reuse_long == 0 || reuse_long < 4 * reuse_short)
        << "steady-state allocation scales with tick count: " << reuse_short
        << " over " << TICKS << " ticks, " << reuse_long << " over "
        << (TICKS * 4);
}

// The same claim on the asynchronous path, which is the one an overlapped frame
// loop actually uses: submit, do unrelated work, settle into a report it owns.
TEST(Integration_SteadyStateAllocation, AsyncWaitIntoAReusedReportIsAmortised)
{
    auto rt  = Runtime::create();
    auto buf = rt.buffer<float>(N);
    for (std::size_t i = 0; i < N; ++i)
        buf[i] = 0.0f;

    float* p = buf.data();
    auto g = rt.graph();
    g.add(API::Reads{}, API::Writes(buf), N,
          [p](std::size_t i) { p[i] = static_cast<float>(i) * 0.25f; });

    Core::RunReport report;
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

#endif // !defined(__SANITIZE_ADDRESS__)
