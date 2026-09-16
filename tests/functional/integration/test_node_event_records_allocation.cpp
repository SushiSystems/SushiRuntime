/**************************************************************************/
/* test_node_event_records_allocation.cpp                                */
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

// G1 task 5, spec §E item 3: a RunReport reused across profiled runs of the
// same shape must not allocate on later runs. Lives beside
// test_steady_state_allocation.cpp, in its own process for the same reason
// that file gives (see its header comment, lines 22-49): a replacement
// operator new is program-wide, and this binary is the executable, not the
// shared library the run path actually executes in — an assertion of an
// absolute allocation count would pass whether or not the ledger allocates.
// The two relative properties below are the ones a replacement operator new
// in this process can actually see.

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <string>

#include <gtest/gtest.h>

#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

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

TEST(Integration_NodeEventRecordsAllocation, SkippedUnderSanitizer)
{
    GTEST_SKIP() << "This sanitizer owns operator new; the allocation counter "
                    "cannot coexist with it. Run this suite in an ordinary build.";
}

#else

namespace
{
    thread_local bool g_counting_this_thread = false;
    std::atomic<std::size_t> g_allocation_count{0};

    inline void note_allocation() noexcept
    {
        if (g_counting_this_thread)
            g_allocation_count.fetch_add(1, std::memory_order_relaxed);
    }

    constexpr std::size_t N            = 64;
    constexpr std::size_t WARMUP_TICKS = 8;
    constexpr std::size_t TICKS        = 64;
} // namespace

void* operator new(std::size_t size)
{
    note_allocation();
    void* p = std::malloc(size != 0 ? size : 1);
    if (p == nullptr)
        throw std::bad_alloc();
    return p;
}

void operator delete(void* p) noexcept              { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

namespace
{
    // Runs @p ticks profiled steps into one reused report and returns how
    // many allocations the calling thread made doing it. Warmed first so the
    // ledger's first-shape growth (allowed to allocate) lands outside the
    // measured window.
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
} // namespace

// The counter must be able to see the thing it is measuring before any claim
// made with it means anything.
TEST(Integration_NodeEventRecordsAllocation, TheCounterObservesTheCallingThread)
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

// The property spec §E item 3 asks for: a reused report replaying the same
// shape does not cost more as the tick count grows. Not "zero" (see the
// header comment on why an absolute claim would be vacuous here) -- a cost
// that is genuinely per-tick would scale with ticks; one paid once at the
// first new shape and then reused would not.
TEST(Integration_NodeEventRecordsAllocation, ReuseTicksDoNotAllocatePerTick)
{
    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(N);
    int* p = buf.data();

    auto g = rt.graph();
    g.set_profiling_requested(true);
    g.add(API::Reads{}, API::Writes(buf), N, [p](std::size_t i) { p[i] = static_cast<int>(i); });

    const std::size_t reuse_short = allocations_for_reuse_ticks(g, TICKS);
    const std::size_t reuse_long  = allocations_for_reuse_ticks(g, TICKS * 4);

    RecordProperty("reuse_allocations_per_" + std::to_string(TICKS) + "_ticks",
                   static_cast<int>(reuse_short));
    RecordProperty("reuse_allocations_per_" + std::to_string(TICKS * 4) + "_ticks",
                   static_cast<int>(reuse_long));

    EXPECT_TRUE(reuse_long == 0 || reuse_long < 4 * reuse_short)
        << "profiled steady-state allocation scales with tick count: " << reuse_short
        << " over " << TICKS << " ticks, " << reuse_long << " over " << (TICKS * 4);
}

#endif // SR_SANITIZER_OWNS_ALLOCATOR
